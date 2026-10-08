
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_intr_alloc.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <soc/gpio_struct.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

constexpr gpio_num_t PIN_BTN_LEFT = GPIO_NUM_0;
constexpr gpio_num_t PIN_BTN_POWER = GPIO_NUM_3;
constexpr gpio_num_t PIN_BTN_RIGHT = GPIO_NUM_7;
constexpr gpio_num_t PIN_EPD_BUSY = GPIO_NUM_6;
constexpr gpio_num_t PIN_EPD_MOSI = GPIO_NUM_11;
constexpr gpio_num_t PIN_EPD_SCLK = GPIO_NUM_12;
constexpr gpio_num_t PIN_EPD_CS = GPIO_NUM_13;
constexpr gpio_num_t PIN_EPD_RST = GPIO_NUM_14;
constexpr gpio_num_t PIN_EPD_DC = GPIO_NUM_18;
constexpr gpio_num_t PIN_LIGHT_COOL = GPIO_NUM_8;
constexpr gpio_num_t PIN_LIGHT_WARM = GPIO_NUM_9;

constexpr uint16_t PANEL_WIDTH = 800;
constexpr uint16_t PANEL_HEIGHT = 480;
constexpr uint16_t CONTROLLER_HEIGHT = 600;
constexpr uint16_t GATE_OFFSET = 120;
constexpr uint16_t ROW_BYTES = PANEL_WIDTH / 8;
// Large DMA transactions do not weaken the electrical abort: the IRAM ISR
// raises CS and holds panel reset low independently of the polling SPI call.
constexpr uint16_t SPI_ROWS_PER_CHUNK = 160;
constexpr size_t SPI_CHUNK_BYTES = ROW_BYTES * SPI_ROWS_PER_CHUNK;
constexpr int SPI_CLOCK_HZ = 20000000;
constexpr int SPI_OVERCLOCK_HZ = 40000000;
constexpr int SPI_TURBO_HZ = 80000000;
constexpr uint32_t BUSY_ASSERT_TIMEOUT_MS = 100;
constexpr uint32_t POWER_TIMEOUT_MS = 1500;
constexpr uint32_t REFRESH_TIMEOUT_MS = 3500;
constexpr uint32_t MIN_VALID_REFRESH_US = 2000;
constexpr uint8_t REQUIRED_LUT_ID_A = 0x68;
constexpr uint8_t REQUIRED_LUT_ID_B = 0x69;

constexpr uint8_t CMD_PANEL_SETTING = 0x00;
constexpr uint8_t CMD_PFS = 0x03;
constexpr uint8_t CMD_POWER_ON = 0x04;
constexpr uint8_t CMD_DTM1 = 0x10;
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;
constexpr uint8_t CMD_DTM2 = 0x13;
constexpr uint8_t CMD_PLL = 0x30;
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;
constexpr uint8_t CMD_TCON = 0x60;
constexpr uint8_t CMD_RESOLUTION = 0x61;
constexpr uint8_t CMD_GATE_SOURCE_START = 0x65;
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x90;
constexpr uint8_t CMD_PARTIAL_IN = 0x91;
constexpr uint8_t CMD_PARTIAL_OUT = 0x92;
constexpr uint8_t CMD_CCSET = 0xE0;
constexpr uint8_t CMD_GATE_SCAN = 0xE1;
constexpr uint8_t CMD_TSSET = 0xE5;

constexpr uint8_t PSR_EXTERNAL = 0x37;
constexpr uint8_t PSR_OTP = 0x17;
constexpr uint8_t PSR_1 = 0x4D;
constexpr uint8_t PFS = 0x20;
constexpr uint8_t GATE_SCAN = 0x02;
constexpr uint8_t CCSET = 0x02;
constexpr uint8_t TSSET_FULL = 0x1E;
constexpr uint8_t TSSET_FAST = 0x5A;
constexpr uint8_t CDI_FULL = 0x97;
constexpr uint8_t CDI_FAST = 0xD7;
constexpr uint8_t TCON_DEFAULT = 0x22;
constexpr uint8_t PLL_STOCK = 0x0E;
constexpr uint8_t PLL_FAST = 0x0F;
constexpr uint8_t PLL_FAST_UPPER_BITS = 0x3F;

constexpr uint8_t LUT_REGS[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
constexpr size_t LUT_BYTES = 42;

// Xteink factory UC8279_aa_prebw_mid tables recovered by FreeInk. This is the
// known external-LUT control before the generated A2 experiments.
constexpr uint8_t STOCK_PREBW[5][LUT_BYTES] = {
    {0x01, 0x06, 0x01, 0x06, 0x06, 0x01, 0x01, 0x01, 0x02, 0x04, 0x00, 0x00, 0x01, 0x01},
    {0x01, 0x06, 0x81, 0x06, 0x06, 0x01, 0x01, 0x01, 0x02, 0x04, 0x00, 0x00, 0x01, 0x01},
    {0x01, 0x86, 0x81, 0x86, 0x86, 0x01, 0x01, 0x01, 0x82, 0x84, 0x00, 0x00, 0x01, 0x01},
    {0x01, 0x46, 0x41, 0x46, 0x46, 0x01, 0x01, 0x01, 0x42, 0x44, 0x00, 0x00, 0x01, 0x01},
    {0x01, 0x06, 0x01, 0x06, 0x06, 0x01, 0x01, 0x01, 0x02, 0x44, 0x00, 0x00, 0x01, 0x01},
};

enum class Waveform : uint8_t { OtpDu, StockPreBw, GeneratedA2, GeneratedAbsoluteA2 };
enum class Pattern : uint8_t { BinaryGrid, TemporalBayer };
enum class Transport : uint8_t { LegacyFullStride, CachedWindow };

struct TestMode {
  const char* name;
  Waveform waveform;
  uint8_t pll;
  uint8_t drive_frames;
  uint8_t counter_frames;
  Pattern pattern;
  Transport transport;
  uint32_t spi_hz;
  uint8_t tcon;
  bool compact_geometry;
  uint16_t window_y;
  uint16_t window_h;
  uint16_t frame_count;
};

// Modes 1-7 all update the same 800x160 physical region. The multiplier names
// are targets, not assumed results: only measured complete BUSY cycles count.
// Compact geometry uses TRES=800x160 plus GSST to select the same physical gate
// band, so it removes inactive gate scanning without reducing the updated area.
constexpr TestMode MODES[] = {
    {"x1 reference / 800x160 / controller-600 partial scan", Waveform::GeneratedAbsoluteA2,
     PLL_FAST, 1, 0, Pattern::TemporalBayer, Transport::CachedWindow, SPI_CLOCK_HZ,
     TCON_DEFAULT, false, 160, 160, 32},
    {"x2 target / 800x160 / compact scan / TCON 0x22", Waveform::GeneratedAbsoluteA2,
     PLL_FAST, 1, 0, Pattern::TemporalBayer, Transport::CachedWindow, SPI_CLOCK_HZ,
     TCON_DEFAULT, true, 160, 160, 48},
    {"x4 target / 800x160 / compact scan / TCON 0x11", Waveform::GeneratedAbsoluteA2,
     PLL_FAST, 1, 0, Pattern::TemporalBayer, Transport::CachedWindow, SPI_CLOCK_HZ,
     0x11, true, 160, 160, 48},
    {"x6 target / 800x160 / compact scan / minimum TCON", Waveform::GeneratedAbsoluteA2,
     PLL_FAST, 1, 0, Pattern::TemporalBayer, Transport::CachedWindow, SPI_CLOCK_HZ,
     0x00, true, 160, 160, 64},
    {"x8 target / 800x160 / compact scan / minimum TCON / 40MHz SPI", Waveform::GeneratedAbsoluteA2,
     PLL_FAST, 1, 0, Pattern::TemporalBayer, Transport::CachedWindow, SPI_OVERCLOCK_HZ,
     0x00, true, 160, 160, 64},
    {"800x160 / compact scan / minimum TCON / 80MHz SPI", Waveform::GeneratedAbsoluteA2,
     PLL_FAST, 1, 0, Pattern::TemporalBayer, Transport::CachedWindow, SPI_TURBO_HZ,
     0x00, true, 160, 160, 64},
    {"800x160 / compact scan / PLL upper-bit probe 0x3F", Waveform::GeneratedAbsoluteA2,
     PLL_FAST_UPPER_BITS, 1, 0, Pattern::TemporalBayer, Transport::CachedWindow, SPI_CLOCK_HZ,
     0x00, true, 160, 160, 64},
    {"800x320 / compact scan / minimum TCON / 40MHz SPI", Waveform::GeneratedAbsoluteA2,
     PLL_FAST, 1, 0, Pattern::TemporalBayer, Transport::CachedWindow, SPI_OVERCLOCK_HZ,
     0x00, true, 80, 320, 32},
    {"800x480 full visible / compact scan / minimum TCON / 40MHz SPI", Waveform::GeneratedAbsoluteA2,
     PLL_FAST, 1, 0, Pattern::TemporalBayer, Transport::CachedWindow, SPI_OVERCLOCK_HZ,
     0x00, true, 0, 480, 24},
};
constexpr size_t MODE_COUNT = sizeof(MODES) / sizeof(MODES[0]);
static_assert(MODE_COUNT <= 9, "single-key serial mode selection supports at most nine modes");
constexpr const char* LAB_VERSION = "0.1.6";

DRAM_ATTR volatile bool abort_requested = false;
DRAM_ATTR volatile uint32_t abort_pin = UINT32_MAX;
DRAM_ATTR volatile bool soft_stop_requested = false;

spi_device_handle_t spi_device = nullptr;
spi_device_handle_t spi_device_nominal = nullptr;
spi_device_handle_t spi_device_overclock = nullptr;
spi_device_handle_t spi_device_turbo = nullptr;
uint8_t* dma_buffer = nullptr;
uint8_t* frame_cache = nullptr;
size_t frame_cache_stride = 0;
size_t frame_cache_count = 0;
bool screen_powered = false;
uint8_t panel_lut_id = 0;
