#include "ProviderEnvironment.hpp"
#include "NativeVideoGray.h"
#include "NativeVideoIdle.h"
#include "NativeVideoStrip.h"
#include "NativeVideoProfile.h"
#include "NativeVideoMono.h"
#include "NativeVideoBootScrub.h"

#include <T5VideoApi.h>



namespace t5s3_epd {
static constexpr uint16_t kPanelWidth = 960;
static constexpr uint16_t kPanelHeight = 540;
static constexpr uint16_t kActiveX = 0;
static constexpr uint16_t kActiveY = 0;
static constexpr uint16_t kActiveWidth = 960;
static constexpr uint16_t kActiveHeight = 540;
}  // namespace t5s3_epd

#ifndef TARGET_FPS
#define TARGET_FPS 24
#endif
#ifndef EPD_BUS_HZ
#define EPD_BUS_HZ 26600000UL
#endif
#ifndef EPD_VIDEO_TOP_DUMMY_LINES
#define EPD_VIDEO_TOP_DUMMY_LINES 0
#endif
#ifndef EPD_VIDEO_BOTTOM_DUMMY_LINES
#define EPD_VIDEO_BOTTOM_DUMMY_LINES 0
#endif

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


namespace {

constexpr const char *kTag = "epd_video";

constexpr size_t kSourceRowBytes = t5s3_epd::kActiveWidth / 8U;
constexpr size_t kStateRowBytes = t5s3_epd::kActiveWidth / 2U;
constexpr size_t kBackbufferBytes =
    static_cast<size_t>(t5s3_epd::kActiveHeight) * kSourceRowBytes;
constexpr size_t kStateBufferBytes =
    static_cast<size_t>(t5s3_epd::kActiveHeight) * kStateRowBytes;
constexpr size_t kGrayRowBytes = t5s3_epd::kActiveWidth / 4U;
constexpr size_t kGrayBufferBytes =
    static_cast<size_t>(t5s3_epd::kActiveHeight) * kGrayRowBytes;
constexpr size_t kGrayStateBytes =
    static_cast<size_t>(t5s3_epd::kActiveHeight) * t5s3_epd::kActiveWidth;
constexpr size_t kPanelRowBytes = t5s3_epd::kPanelWidth / 4U;
constexpr size_t kLinePaddingBytes = 0;
constexpr size_t kDmaRowBytes = kPanelRowBytes + kLinePaddingBytes;
constexpr size_t kActiveLeftPadBytes = t5s3_epd::kActiveX / 4U;
constexpr size_t kActiveRowBytes = t5s3_epd::kActiveWidth / 4U;
constexpr size_t kActiveRightPadBytes = kPanelRowBytes - kActiveLeftPadBytes - kActiveRowBytes;
// Raw pixel-only EPD has no D/C wire; never borrow radio CS46.
constexpr int kDummyDcGpio = -1;
constexpr gpio_num_t kWrGpio = GPIO_NUM_4;
constexpr gpio_num_t kCsGpio = GPIO_NUM_41;
constexpr gpio_num_t kLeGpio = GPIO_NUM_42;
constexpr gpio_num_t kStvGpio = GPIO_NUM_45;
constexpr gpio_num_t kCkvGpio = GPIO_NUM_48;
constexpr gpio_num_t kDataGpios[8] = {
    GPIO_NUM_5,
    GPIO_NUM_6,
    GPIO_NUM_7,
    GPIO_NUM_15,
    GPIO_NUM_16,
    GPIO_NUM_17,
    GPIO_NUM_18,
    GPIO_NUM_8,
};
static_assert(kStateRowBytes%4U==0, "mono state rows must preserve word alignment");
static_assert((t5s3_epd::kActiveWidth % 8U) == 0U, "active width must be byte aligned");
static_assert((t5s3_epd::kPanelWidth % 4U) == 0U, "panel width must be 2bpp packed");
static_assert(
    (kActiveLeftPadBytes + kActiveRowBytes + kActiveRightPadBytes) == kPanelRowBytes,
    "active area padding must cover the whole panel line");

const risc_display_power_api_v1* g_power = nullptr;
uint64_t g_power_grant = 0;
risc_cpu_worker_v2 g_scan_task = 0;
esp_lcd_i80_bus_handle_t g_i80_bus = nullptr;
esp_lcd_panel_io_handle_t g_panel_io = nullptr;
portMUX_TYPE g_buffer_lock = portMUX_INITIALIZER_UNLOCKED;

uint8_t *g_buffers[2] = {nullptr, nullptr};
uint8_t *g_state_buffer = nullptr;
// Shared startup waveform for both mono and grayscale video clients.
uint8_t *g_boot_scrub_map = nullptr;
int g_boot_scrub_step = -1;  // Protected by g_buffer_lock; scan snapshots once/frame.
uint8_t *g_dma_buf[2] = {nullptr, nullptr};
uint8_t *g_blank_row = nullptr;
uint8_t g_pixel_format = T5_VIDEO_PIXEL_MONO_1BPP_MSB;
size_t g_source_row_bytes = kSourceRowBytes;
size_t g_backbuffer_bytes = kBackbufferBytes;
size_t g_state_row_bytes = kStateRowBytes;
size_t g_state_buffer_bytes = kStateBufferBytes;
uint8_t g_row_active[t5s3_epd::kActiveHeight] = {0};
NativeVideoBlackStrip<t5s3_epd::kActiveHeight> g_black_strip; // scan-thread owned
bool g_strip_request=false; // request fields protected by g_buffer_lock
uint16_t g_strip_y=0,g_strip_height=0;uint8_t g_strip_passes=0;

volatile bool g_running = false;
volatile bool g_dma_done = true;
volatile bool g_flip_req = false;
volatile bool g_drive_pending = false;
volatile uint8_t g_front_index = 0;
volatile uint32_t g_vsync_count = 0;
volatile uint32_t g_submit_count = 0;
t5_video_scan_stats_v1 g_scan_stats{};
DRAM_ATTR NativeVideoMonoTable g_mono_table;
uint8_t g_app_core=0, g_scan_core=0;
uint16_t g_pending_dirty_start = 0;
uint16_t g_pending_dirty_end = t5s3_epd::kActiveHeight - 1;

bool dma_done_callback(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx) {
  (void)panel_io;
  (void)edata;
  (void)user_ctx;
  // End gray row selection before the CPU finishes preparing the next row.
  // Previously that variable preparation time extended CKV high, changing the
  // drive dose with scene complexity. Mono retains its established timing.
  if (g_pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB) {
    gpio_set_level(kCkvGpio, 0);
  }
  g_dma_done = true;
  return false;
}

void free_buffer(uint8_t *&buffer) {
  if (buffer != nullptr) {
    heap_caps_free(buffer);
    buffer = nullptr;
  }
}

void release_allocations() {
  free_buffer(g_boot_scrub_map);
  g_boot_scrub_step = -1;
  free_buffer(g_buffers[0]);
  free_buffer(g_buffers[1]);
  free_buffer(g_state_buffer);
  free_buffer(g_dma_buf[0]);
  free_buffer(g_dma_buf[1]);
  free_buffer(g_blank_row);
}



uint8_t *alloc_8bit_buffer(size_t size, bool prefer_external) {
  uint8_t *buffer = nullptr;

  if (prefer_external) {
    buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
      buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
  } else {
    buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
      buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
  }

  if (buffer == nullptr) {
    buffer = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
  }
  return buffer;
}

bool alloc_video_buffers() {
  const bool prefer_external_for_framebuffers =
      (kBackbufferBytes >= (48U * 1024U)) || (kStateBufferBytes >= (128U * 1024U));

  for (uint8_t i = 0; i < 2; ++i) {
    g_buffers[i] = alloc_8bit_buffer(g_backbuffer_bytes, prefer_external_for_framebuffers);
    if (g_buffers[i] == nullptr) {
      ESP_LOGE(kTag, "failed to allocate framebuffer %u", i);
      return false;
    }
    memset(g_buffers[i], 0xFF, g_backbuffer_bytes);
  }

  g_state_buffer = alloc_8bit_buffer(g_state_buffer_bytes, true);
  if (g_state_buffer == nullptr) {
    ESP_LOGE(kTag, "failed to allocate state buffer");
    return false;
  }
  memset(g_state_buffer, g_pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB
      ? kNativeVideoGrayUnknown : 0x00, g_state_buffer_bytes);

  for (uint8_t i = 0; i < 2; ++i) {
    g_dma_buf[i] = static_cast<uint8_t *>(
        heap_caps_malloc(kDmaRowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (g_dma_buf[i] == nullptr) {
      ESP_LOGE(kTag, "failed to allocate dma row buffer %u", i);
      return false;
    }
    memset(g_dma_buf[i], 0x00, kDmaRowBytes);
  }

  g_blank_row = static_cast<uint8_t *>(
      heap_caps_malloc(kDmaRowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  if (g_blank_row == nullptr) {
    ESP_LOGE(kTag, "failed to allocate blank row buffer");
    return false;
  }
  memset(g_blank_row, 0x00, kDmaRowBytes);

  return true;
}

void sanitize_dirty_region(uint16_t dirty_y, uint16_t dirty_height, uint16_t &row_start, uint16_t &row_end) {
  row_start = 0;
  row_end = t5s3_epd::kActiveHeight - 1;

  if (dirty_height == 0U) {
    return;
  }

  row_start = dirty_y;
  if (row_start >= t5s3_epd::kActiveHeight) {
    row_start = t5s3_epd::kActiveHeight - 1;
  }

  const uint32_t last_row = static_cast<uint32_t>(dirty_y) + dirty_height - 1U;
  row_end = static_cast<uint16_t>(
      (last_row >= t5s3_epd::kActiveHeight) ? (t5s3_epd::kActiveHeight - 1U) : last_row);
}

void mark_rows_active(uint16_t row_start, uint16_t row_end) {
  for (uint16_t row = row_start; row <= row_end; ++row) {
    g_row_active[row] = 1U;
  }
}

void configure_idle_levels() {
  gpio_set_level(kLeGpio, 0);
  gpio_set_level(kStvGpio, 1);
  gpio_set_level(kCkvGpio, 1);
  gpio_set_level(kCsGpio, 1);
}

bool init_control_gpios() {
  gpio_config_t config = {};
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  config.pin_bit_mask =
      (1ULL << kCsGpio) |
      (1ULL << kLeGpio) |
      (1ULL << kStvGpio) |
      (1ULL << kCkvGpio);

  const esp_err_t err = gpio_config(&config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "gpio_config failed: %d", (int)err);
    return false;
  }

  configure_idle_levels();
  return true;
}

bool init_panel_bus() {
  if (g_panel_io != nullptr) {
    return true;
  }

  if (!init_control_gpios()) {
    return false;
  }

  esp_lcd_i80_bus_config_t bus_config = {};
  bus_config.dc_gpio_num = static_cast<int>(kDummyDcGpio);
  bus_config.wr_gpio_num = static_cast<int>(kWrGpio);
  bus_config.clk_src = LCD_CLK_SRC_PLL160M;
  for (size_t i = 0; i < 8; ++i) {
    bus_config.data_gpio_nums[i] = static_cast<int>(kDataGpios[i]);
  }
  bus_config.bus_width = 8;
  bus_config.max_transfer_bytes = kDmaRowBytes;

  esp_err_t err = esp_lcd_new_i80_bus(&bus_config, &g_i80_bus);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_lcd_new_i80_bus failed: %d", (int)err);
    return false;
  }

  esp_lcd_panel_io_i80_config_t panel_config = {};
  panel_config.cs_gpio_num = static_cast<int>(kCsGpio);
  panel_config.pclk_hz = EPD_BUS_HZ;
  panel_config.trans_queue_depth = 4;
  panel_config.on_color_trans_done = dma_done_callback;
  panel_config.user_ctx = nullptr;
  panel_config.lcd_cmd_bits = 8;
  panel_config.lcd_param_bits = 8;
  // No D/C pin is connected; phase values remain internal to LCD.
  panel_config.dc_levels.dc_idle_level = 1;
  panel_config.dc_levels.dc_cmd_level = 1;
  panel_config.dc_levels.dc_dummy_level = 1;
  panel_config.dc_levels.dc_data_level = 1;
  panel_config.flags.cs_active_high = 0;
  panel_config.flags.reverse_color_bits = 0;
  panel_config.flags.swap_color_bytes = 0;
  panel_config.flags.pclk_active_neg = 0;
  panel_config.flags.pclk_idle_low = 0;

  err = esp_lcd_new_panel_io_i80(g_i80_bus, &panel_config, &g_panel_io);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_lcd_new_panel_io_i80 failed: %d", (int)err);
    return false;
  }

  g_dma_done = true;
  return true;
}

void row_control_start() {
  gpio_set_level(kCkvGpio, 1);
  delayMicroseconds(7);
  gpio_set_level(kStvGpio, 0);
  delayMicroseconds(10);
  gpio_set_level(kCkvGpio, 0);
  gpio_set_level(kCkvGpio, 1);
  delayMicroseconds(8);
  gpio_set_level(kStvGpio, 1);
  delayMicroseconds(10);
  gpio_set_level(kCkvGpio, 0);

  for (uint8_t i = 0; i < 3; ++i) {
    gpio_set_level(kCkvGpio, 1);
    delayMicroseconds(18);
    gpio_set_level(kCkvGpio, 0);
  }

  gpio_set_level(kCkvGpio, 1);
}

void row_control_step() {
  gpio_set_level(kCkvGpio, 0);
  gpio_set_level(kLeGpio, 1);
  gpio_set_level(kLeGpio, 0);
}

bool wait_for_dma(uint64_t *elapsed_us = nullptr) {
  const int64_t start = esp_timer_get_time();
  int64_t yielded = start;
  while (!g_dma_done) {
    const int64_t now = esp_timer_get_time();
    if (now - start >= 100000) {
      ESP_LOGE(kTag, "DMA completion timeout; retaining live resources");
      g_running = false;
      return false;
    }
    if (now - yielded >= 1000) { vTaskDelay(1); yielded = now; }
    else delayMicroseconds(1);
  }
  if (elapsed_us) *elapsed_us += static_cast<uint64_t>(esp_timer_get_time()-start);
  return true;
}

bool send_row(uint8_t *data, bool first_row, uint64_t &dma_wait_us) {
  if (!wait_for_dma(&dma_wait_us)) return false;
  if (!first_row) {
    row_control_step();
  }

  g_dma_done = false;
  gpio_set_level(kCkvGpio, 1);
  const esp_err_t err = esp_lcd_panel_io_tx_color(g_panel_io, -1, data, kDmaRowBytes);
  if (err != ESP_OK) {
    gpio_set_level(kCkvGpio, 0);
    g_dma_done = true;
    ESP_LOGE(kTag, "row transmit failed: %d", (int)err);
    return false;
  }
  return true;
}

// Each state byte tracks two pixels: direction bits in the LSBs and independent
// pulse counters in the upper nibbles. Three complete scans improve black
// density; a frame remains pending until all three scans have completed.
bool build_active_row(const uint8_t *frame, uint16_t row, uint8_t *dst, bool &target_changed) {
  if (g_pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB) {
    const uint8_t *source = frame + static_cast<size_t>(row) * g_source_row_bytes;
    uint8_t *state = g_state_buffer + static_cast<size_t>(row) * g_state_row_bytes;
    uint8_t *drive = dst + kActiveLeftPadBytes;
    const bool pending = nativeVideoBuildGrayRow(source, state, drive, kGrayRowBytes, &target_changed);
    g_row_active[row] = pending ? 1U : 0U;
    return pending;
  }
  const unsigned flags=g_mono_table.row(
      frame+static_cast<size_t>(row)*g_source_row_bytes,
      g_state_buffer+static_cast<size_t>(row)*g_state_row_bytes,
      dst+kActiveLeftPadBytes,kSourceRowBytes);
  target_changed=target_changed || (flags&1U);
  const bool needs_more_drive=(flags&2U)!=0;

  g_row_active[row] = needs_more_drive ? 1U : 0U;
  return needs_more_drive;
}

uint8_t *prepare_scan_row(
    const uint8_t *frame,
    uint16_t scan_row,
    uint8_t dma_index,
    uint32_t &processed_rows,
    uint32_t &continuing_rows,
    bool &target_changed,
    int cleanup_phase, int scrub_step) {
  if (scan_row < EPD_VIDEO_TOP_DUMMY_LINES) {
    return g_blank_row;
  }

  const uint16_t active_row = static_cast<uint16_t>(scan_row - EPD_VIDEO_TOP_DUMMY_LINES);
  if (active_row >= t5s3_epd::kActiveHeight) {
    return g_blank_row;
  }

  if (scrub_step >= 0) {
    if (scrub_step >= static_cast<int>(NativeVideoBootScrub::kScans)) return g_blank_row;
    uint8_t* row = g_dma_buf[dma_index];
    NativeVideoBootScrub::driveRow(g_boot_scrub_map, active_row,
                                  static_cast<unsigned>(scrub_step), row + kActiveLeftPadBytes);
    ++processed_rows;
    ++continuing_rows;
    return row;
  }
  const bool active = g_row_active[active_row] != 0U;
  const bool strip = g_black_strip.active(active_row);
  const bool cleanup = !strip && !target_changed && nativeVideoIdleRowSelected(active_row, cleanup_phase);
  if (!active && !cleanup && !strip) return g_blank_row;

  uint8_t *row = g_dma_buf[dma_index];
  if (active) {
    ++processed_rows;
    if (build_active_row(frame, active_row, row, target_changed)) ++continuing_rows;
  } else {
    memset(row + kActiveLeftPadBytes, 0, kActiveRowBytes);
  }
  if(strip) {
    const size_t count=g_black_strip.row(active_row,
        frame+static_cast<size_t>(active_row)*g_source_row_bytes,
        g_state_buffer+static_cast<size_t>(active_row)*g_state_row_bytes,
        row+kActiveLeftPadBytes,t5s3_epd::kActiveWidth,g_row_active[active_row]!=0);
    if(!active) {if(!count)return g_blank_row;++processed_rows;}
  }
  if (cleanup && !target_changed) {
    const size_t count = nativeVideoReinforceIdleRow(
        frame + static_cast<size_t>(active_row) * g_source_row_bytes,
        g_state_buffer + static_cast<size_t>(active_row) * g_state_row_bytes,
        row + kActiveLeftPadBytes, t5s3_epd::kActiveWidth,
        g_pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB, active_row, cleanup_phase);
    if (!active && count == 0) return g_blank_row;
  }
  return row;
}

void sleep_to_target_frame(int64_t frame_start_us) {
  if (TARGET_FPS <= 0) {
    return;
  }

  const int64_t target_us = 1000000LL / TARGET_FPS;
  while (true) {
    const int64_t elapsed_us = esp_timer_get_time() - frame_start_us;
    const int64_t remaining_us = target_us - elapsed_us;
    if (remaining_us <= 0) {
      return;
    }
    if (remaining_us > 2000) {
      vTaskDelay(1);
    } else {
      delayMicroseconds(static_cast<unsigned int>(remaining_us));
      return;
    }
  }
}

void scan_task(void *unused) {
  (void)unused;

  ESP_LOGI(
      kTag,
      "raw scan task started on core %d, target=%d fps, active=%ux%u",
      xPortGetCoreID(),
      TARGET_FPS,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight);

  const uint16_t total_scan_rows =
      EPD_VIDEO_TOP_DUMMY_LINES + t5s3_epd::kActiveHeight + EPD_VIDEO_BOTTOM_DUMMY_LINES;
  uint64_t log_window_start = esp_timer_get_time();
  uint64_t log_scan_us = 0;
  uint32_t log_frames = 0;
  uint32_t last_submit_count = 0;
  NativeVideoProfile profile;
  profile.reset(log_window_start);
  NativeVideoIdleCleanup idle_cleanup;
  idle_cleanup.reset(static_cast<uint32_t>(esp_timer_get_time() / 1000));

  while (g_running) {
    const int64_t frame_start_us = esp_timer_get_time();
    uint8_t front_index = 0;
    uint32_t submitted_frames = 0;
    uint16_t dirty_start = 0;
    uint16_t dirty_end = 0;
    bool applied_flip = false,strip_request=false;
    uint16_t strip_y=0,strip_height=0;uint8_t strip_passes=0;
    int scrub_step;

    portENTER_CRITICAL(&g_buffer_lock);
    scrub_step = g_boot_scrub_step;
    strip_request=g_strip_request;strip_y=g_strip_y;strip_height=g_strip_height;strip_passes=g_strip_passes;
    g_strip_request=false;
    ++g_vsync_count;
    if (g_flip_req) {
      g_front_index ^= 1U;
      g_flip_req = false;
      ++g_submit_count;
      dirty_start = g_pending_dirty_start;
      dirty_end = g_pending_dirty_end;
      applied_flip = true;
    }
    front_index = g_front_index;
    submitted_frames = g_submit_count;
    portEXIT_CRITICAL(&g_buffer_lock);

    g_black_strip.finishScan();
    if(strip_request)g_black_strip.request(strip_y,strip_height,strip_passes);
    if (applied_flip) {
      mark_rows_active(dirty_start, dirty_end);
    }

    const uint8_t *frame = g_buffers[front_index];
    uint64_t prepare_us=0, dma_wait_us=0;
    uint32_t processed_rows = 0;
    uint32_t continuing_rows = 0;
    bool target_changed = false;
    // Content changes reset the quiet timer, not repeated identical submits.
    // Normal row processing detects changes before optional reinforcement.
    const int cleanup_phase = submitted_frames ?
        idle_cleanup.phase(static_cast<uint32_t>(frame_start_us / 1000)) : -1;

    row_control_start();

    uint8_t dma_index = 0;
    int64_t prepare_start=esp_timer_get_time();
    uint8_t *row_ptr = prepare_scan_row(frame, 0, dma_index, processed_rows, continuing_rows, target_changed, cleanup_phase, scrub_step);
    prepare_us+=static_cast<uint64_t>(esp_timer_get_time()-prepare_start);
    bool row_uses_dma = (row_ptr != g_blank_row);
    if (!send_row(row_ptr, true, dma_wait_us)) {
      g_running = false;
      break;
    }

    for (uint16_t scan_row = 1; scan_row < total_scan_rows; ++scan_row) {
      const uint8_t next_dma_index = row_uses_dma ? static_cast<uint8_t>(dma_index ^ 1U) : dma_index;
      prepare_start=esp_timer_get_time();
      uint8_t *next_row_ptr =
          prepare_scan_row(frame, scan_row, next_dma_index, processed_rows, continuing_rows, target_changed, cleanup_phase, scrub_step);
      prepare_us+=static_cast<uint64_t>(esp_timer_get_time()-prepare_start);
      const bool next_row_uses_dma = (next_row_ptr != g_blank_row);
      if (!send_row(next_row_ptr, false, dma_wait_us)) {
        g_running = false;
        break;
      }
      dma_index = next_dma_index;
      row_uses_dma = next_row_uses_dma;
    }

    if (!g_running) {
      break;
    }

    if (!send_row(g_blank_row, false, dma_wait_us)) {
      g_running = false;
      break;
    }
    if (!wait_for_dma(&dma_wait_us)) break;
    portENTER_CRITICAL(&g_buffer_lock);
    // A submit can arrive during this scan. Idle waits must include both
    // queued frames and unfinished pulses, even though frame admission only
    // waits for the queued flip to release the back buffer.
    if (scrub_step >= 0 && scrub_step < static_cast<int>(NativeVideoBootScrub::kScans))
      g_boot_scrub_step = scrub_step + 1;
    g_drive_pending = (continuing_rows != 0U) || g_flip_req;
    portEXIT_CRITICAL(&g_buffer_lock);

    // Cleanup is opportunistic: it neither extends g_drive_pending nor blocks
    // a queued target. At most this already-running scan precedes a new flip.
    idle_cleanup.finishScan(static_cast<uint32_t>(esp_timer_get_time() / 1000), target_changed, cleanup_phase);
    ++log_frames;
    log_scan_us += static_cast<uint64_t>(esp_timer_get_time() - frame_start_us);

    const uint64_t now = esp_timer_get_time();
    if ((now - log_window_start) >= 1000000ULL) {
      const uint32_t avg_scan_ms =
          (log_frames == 0U) ? 0U : static_cast<uint32_t>((log_scan_us / log_frames) / 1000ULL);
      ESP_LOGI(
          kTag,
          "scan=%u fps submitted=%u fps avg_scan=%u ms drive_rows=%lu keep_rows=%lu flip=%s vsync=%lu",
          log_frames,
          submitted_frames - last_submit_count,
          avg_scan_ms,
          static_cast<unsigned long>(processed_rows),
          static_cast<unsigned long>(continuing_rows),
          applied_flip ? "yes" : "no",
          static_cast<unsigned long>(g_vsync_count));
      last_submit_count = submitted_frames;
      log_frames = 0;
      log_scan_us = 0;
      log_window_start = now;
    }

    const int64_t pace_start=esp_timer_get_time();
    const uint64_t scan_us=static_cast<uint64_t>(pace_start-frame_start_us);
    sleep_to_target_frame(frame_start_us);
    const uint64_t finished=static_cast<uint64_t>(esp_timer_get_time());
    t5_video_scan_stats_v1 snapshot{};
    if(profile.record(finished,scan_us,prepare_us,dma_wait_us,
                      finished-static_cast<uint64_t>(pace_start),processed_rows,snapshot)) {
      snapshot.app_core=g_app_core; snapshot.scan_core=g_scan_core;
      portENTER_CRITICAL(&g_buffer_lock);
      g_scan_stats=snapshot;
      portEXIT_CRITICAL(&g_buffer_lock);
    }
  }

  return; // Resident CPU trampoline publishes completion after ELF return.
}

}  // namespace

bool epd_video_init() {
  if (g_power) return false;
  g_power = display_power;
  if (!g_power || g_power->api_version != 1 || g_power->struct_size < sizeof(*g_power) ||
      !g_power->acquire || !g_power->release) { g_power = nullptr; return false; }

  if (!init_panel_bus()) {
    return false;
  }

  // Initialize the LCD/GDMA bus before large frame allocations so the driver
  // can reserve its internal resources from an unfragmented heap.
  if (!alloc_video_buffers()) {
    release_allocations();
    return false;
  }

  return true;
}

bool epd_video_power_on() {
  if (!g_power || g_power_grant || !g_power->acquire(g_power->context, &g_power_grant)) return false;

  memset(g_buffers[0], 0xFF, g_backbuffer_bytes);
  memset(g_buffers[1], 0xFF, g_backbuffer_bytes);
  memset(g_state_buffer, g_pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB
      ? kNativeVideoGrayUnknown : 0x00, g_state_buffer_bytes);
  memset(g_dma_buf[0], 0x00, kDmaRowBytes);
  memset(g_dma_buf[1], 0x00, kDmaRowBytes);
  memset(g_blank_row, 0x00, kDmaRowBytes);
  memset(g_row_active, 0x00, sizeof(g_row_active));
  g_black_strip.reset();g_strip_request=false;
  g_drive_pending = false;
  configure_idle_levels();

  return true;
}

bool epd_video_start() {
  if (g_running) {
    return true;
  }

  portENTER_CRITICAL(&g_buffer_lock);
  g_front_index = 0;
  g_vsync_count = 0;
  g_submit_count = 0;
  g_flip_req = false;
  g_pending_dirty_start = 0;
  g_pending_dirty_end = t5s3_epd::kActiveHeight - 1;
  portEXIT_CRITICAL(&g_buffer_lock);

  if(g_pixel_format==T5_VIDEO_PIXEL_MONO_1BPP_MSB) g_mono_table.init();
  g_app_core=static_cast<uint8_t>(xPortGetCoreID());
#if CONFIG_FREERTOS_UNICORE
  g_scan_core=g_app_core;
#else
  g_scan_core=static_cast<uint8_t>(1U-g_app_core);
#endif
  portENTER_CRITICAL(&g_buffer_lock);
  g_scan_stats={};
  portEXIT_CRITICAL(&g_buffer_lock);
  g_dma_done = true;
  g_running = true;
  memset(g_row_active, 0x00, sizeof(g_row_active));
  g_black_strip.reset();g_strip_request=false;
  g_drive_pending = false;

  const int rc = risc_cpu_worker_start_v2(scan_task,nullptr,8192,3,g_scan_core,&g_scan_task);
  if (rc != RISC_CPU_WORKER_OK) {
    g_running = false;
    ESP_LOGE(kTag, "failed to create raw scan task");
    return false;
  }
  return true;
}

uint8_t *epd_video_get_backbuffer() {
  uint8_t back_index = 0;
  portENTER_CRITICAL(&g_buffer_lock);
  back_index = g_front_index ^ 1U;
  portEXIT_CRITICAL(&g_buffer_lock);
  return g_buffers[back_index];
}

size_t epd_video_get_backbuffer_size() {
  return g_backbuffer_bytes;
}


bool epd_video_submit(uint16_t dirty_y, uint16_t dirty_height) {
  if (!g_running) {
    return false;
  }

  uint16_t row_start = 0;
  uint16_t row_end = 0;
  sanitize_dirty_region(dirty_y, dirty_height, row_start, row_end);

  bool accepted = false;
  portENTER_CRITICAL(&g_buffer_lock);
  if (nativeVideoCanQueueFrame(g_running, g_flip_req,
                              g_pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB,
                              g_drive_pending)) {
    g_pending_dirty_start = row_start;
    g_pending_dirty_end = row_end;
    g_flip_req = true;
    g_drive_pending = true;
    accepted = true;
  }
  portEXIT_CRITICAL(&g_buffer_lock);
  return accepted;
}

bool epd_video_can_submit() {
  bool ready = false;
  portENTER_CRITICAL(&g_buffer_lock);
  // A free backbuffer can accept a new target before gray settling finishes.
  // Per-pixel state retains every pulse sent toward the previous target.
  // Use the same admission rule here and inside submit's critical section.
  ready = nativeVideoCanQueueFrame(g_running, g_flip_req,
                                  g_pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB,
                                  g_drive_pending);
  portEXIT_CRITICAL(&g_buffer_lock);
  return ready;
}

bool epd_video_submit_pending() {
  bool pending = false;
  portENTER_CRITICAL(&g_buffer_lock);
  pending = g_flip_req || g_drive_pending;
  portEXIT_CRITICAL(&g_buffer_lock);
  return pending;
}

uint32_t epd_video_get_vsync_count() {
  return g_vsync_count;
}

bool epd_video_shutdown() {
  g_running = false;
  g_drive_pending = false;

  portENTER_CRITICAL(&g_buffer_lock);
  g_flip_req = false;
  portEXIT_CRITICAL(&g_buffer_lock);


  if (g_scan_task) {
    if (risc_cpu_worker_join_v2(g_scan_task,2000) != RISC_CPU_WORKER_OK ||
        risc_cpu_worker_release_v2(g_scan_task) != RISC_CPU_WORKER_OK) return false;
    g_scan_task = 0;
  }
  if (!wait_for_dma()) return false;
  configure_idle_levels();

  if (g_power_grant) {
    if (!g_power || !g_power->release(g_power->context, g_power_grant)) return false;
    g_power_grant = 0;
  }
  if (g_panel_io != nullptr) {
    const esp_err_t rc = esp_lcd_panel_io_del(g_panel_io);
    if (rc != ESP_OK) {
      ESP_LOGE(kTag, "panel IO release failed: %d", (int)rc);
      return false;
    } else {
      g_panel_io = nullptr;
    }
  }
  if (g_i80_bus != nullptr) {
    const esp_err_t rc = esp_lcd_del_i80_bus(g_i80_bus);
    if (rc != ESP_OK) {
      ESP_LOGE(kTag, "i80 bus release failed: %d", (int)rc);
      return false;
    } else {
      g_i80_bus = nullptr;
    }
  }
  release_allocations();
  g_power = nullptr;
  g_dma_done = true;
  g_flip_req = false;
  g_drive_pending = false;
  return true;
}

namespace {
bool s_video_started = false;


bool video_start_format(t5_video_surface_v1 *surface, uint8_t pixel_format) {
  if (pixel_format != T5_VIDEO_PIXEL_MONO_1BPP_MSB &&
      pixel_format != T5_VIDEO_PIXEL_GRAY_2BPP_MSB) return false;
  if (s_video_started && g_pixel_format != pixel_format) return false;
  if (!s_video_started) {
    // A failed previous teardown retains live handles/buffers. Do not overwrite
    // their format or initialize a second owner on top of them.
    if (g_scan_task || g_panel_io || g_i80_bus || g_power || g_power_grant) return false;
    g_pixel_format = pixel_format;
    g_source_row_bytes = pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB
        ? kGrayRowBytes : kSourceRowBytes;
    g_backbuffer_bytes = pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB
        ? kGrayBufferBytes : kBackbufferBytes;
    g_state_row_bytes = pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB
        ? t5s3_epd::kActiveWidth : kStateRowBytes;
    g_state_buffer_bytes = pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB
        ? kGrayStateBytes : kStateBufferBytes;
    if (!epd_video_init() ||
        !epd_video_power_on()) {
      ESP_LOGE("FAST_VIDEO", "GameBoy-derived raw EPD video start failed");
      epd_video_shutdown();
      return false;
    }

    {
      g_boot_scrub_map = static_cast<uint8_t*>(heap_caps_malloc(
          NativeVideoBootScrub::kMapBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!g_boot_scrub_map) { epd_video_shutdown(); return false; }
      const uint32_t map_start = millis();
      uint32_t checkpoint = map_start;
      for (unsigned y = 0; y < NativeVideoBootScrub::kHeight; y += NativeVideoBootScrub::kGrid) {
        if (static_cast<uint32_t>(millis() - map_start) >= 1500U) {
          ESP_LOGE(kTag, "video scrub preparation timed out");
          epd_video_shutdown(); return false;
        }
        NativeVideoBootScrub::buildBand(g_boot_scrub_map, y);
        if ((y % 32U) == 0U || static_cast<uint32_t>(millis() - checkpoint) >= 8U) {
          vTaskDelay(1); checkpoint = millis();
        }
      }
      g_boot_scrub_step = 0;  // No scan task yet.
    }
    if (!epd_video_start()) { epd_video_shutdown(); return false; }
    {
      const uint32_t scrub_start = millis();
      bool complete = false;
      do {
        portENTER_CRITICAL(&g_buffer_lock);
        complete = g_boot_scrub_step >= static_cast<int>(NativeVideoBootScrub::kScans);
        portEXIT_CRITICAL(&g_buffer_lock);
        if (complete) break;
        vTaskDelay(1);
      } while (g_running && static_cast<uint32_t>(millis() - scrub_start) < 1800U);
      if (!complete) {
        ESP_LOGE(kTag, "video scrub scan failed or exceeded deadline");
        epd_video_shutdown(); return false;
      }
      // The last white pulses have drained through DMA. While the terminal step is latched all
      // scans retain the panel, so seed the normal engine with settled white.
      memset(g_buffers[0], 0, g_backbuffer_bytes);
      memset(g_buffers[1], 0, g_backbuffer_bytes);
      memset(g_state_buffer, g_pixel_format == T5_VIDEO_PIXEL_GRAY_2BPP_MSB
          ? 0x00 : 0xfc, g_state_buffer_bytes);
      // The terminal step never dereferences the map; the final drive has drained. Release
      // its 506 KiB before clients begin rendering or allocating their scene buffers.
      free_buffer(g_boot_scrub_map);
      portENTER_CRITICAL(&g_buffer_lock);
      g_boot_scrub_step = -1;
      portEXIT_CRITICAL(&g_buffer_lock);
      ESP_LOGI(kTag, "video wisp scrub complete: %lu ms, %u scans", static_cast<unsigned long>(millis()-scrub_start), NativeVideoBootScrub::kScans);
    }
    s_video_started = true;
    ESP_LOGI("FAST_VIDEO", "raw EPD video ready: 960x540 @ %d fps scan target", TARGET_FPS);
  }

  if (surface) {
    surface->width = t5s3_epd::kActiveWidth;
    surface->height = t5s3_epd::kActiveHeight;
    surface->stride_bytes = static_cast<uint16_t>(g_source_row_bytes);
    surface->pixel_format = g_pixel_format;
    surface->flags = T5_VIDEO_FLAG_ONE_IS_BLACK;
  }
  return true;
}

bool video_start(t5_video_surface_v1 *surface) {
  return video_start_format(surface, T5_VIDEO_PIXEL_MONO_1BPP_MSB);
}

uint8_t *video_backbuffer(size_t *size_out) {
  if (size_out) *size_out = s_video_started ? epd_video_get_backbuffer_size() : 0U;
  return s_video_started ? epd_video_get_backbuffer() : nullptr;
}

bool video_can_submit() {
  return s_video_started && epd_video_can_submit();
}

bool video_submit(uint16_t dirty_y, uint16_t dirty_height) {
  return s_video_started && epd_video_submit(dirty_y, dirty_height);
}

bool video_pending() {
  return s_video_started && epd_video_submit_pending();
}

uint32_t video_frame_counter() {
  return s_video_started ? epd_video_get_vsync_count() : 0U;
}

void video_stop() {
  if (!s_video_started && !g_i80_bus && !g_panel_io && !g_power && !g_power_grant) return;
  if (epd_video_shutdown()) s_video_started = false;
}

bool video_scan_stats(t5_video_scan_stats_v1 *out) {
  if(!out || !s_video_started) return false;
  portENTER_CRITICAL(&g_buffer_lock);
  *out=g_scan_stats;
  portEXIT_CRITICAL(&g_buffer_lock);
  return out->samples!=0;
}

bool video_reinforce_black(uint16_t y,uint16_t height,uint8_t passes) {
  if(passes>2 || (passes && (!height || height>64 || y>=t5s3_epd::kActiveHeight || height>t5s3_epd::kActiveHeight-y)))return false;
  bool accepted=false;
  portENTER_CRITICAL(&g_buffer_lock);
  if(s_video_started && g_running && g_pixel_format==T5_VIDEO_PIXEL_MONO_1BPP_MSB) {
    g_strip_y=y;g_strip_height=height;g_strip_passes=passes;g_strip_request=true;accepted=true;
  }
  portEXIT_CRITICAL(&g_buffer_lock);
  return accepted;
}

const t5_video_api_v1 s_api = {
    T5_VIDEO_API_VERSION,
    sizeof(t5_video_api_v1),
    video_start,
    video_backbuffer,
    video_can_submit,
    video_submit,
    video_pending,
    video_frame_counter,
    video_stop,
    video_start_format,
    video_scan_stats,
    video_reinforce_black,
    display_fast_force_stop,
};
}  // namespace

extern "C" const t5_video_api_v1 *display_fast_api(uint32_t api_version) {
  // Capability discovery is safe before takeover; start_format still enforces ownership.
  if (api_version != T5_VIDEO_API_VERSION) return nullptr;
  return &s_api;
}

bool display_fast_force_stop() {
  video_stop();
  return !s_video_started && !g_scan_task && !g_panel_io && !g_i80_bus && !g_power && !g_power_grant;
}
