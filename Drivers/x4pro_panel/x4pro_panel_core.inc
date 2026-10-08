/* X4 Pro 800x480 panel provider for SSD1677 and UC8279 variants.
 *
 * x4pro-panel 0.2.0 integrates the validated X4LAB v0.1.5 UC8279 fast path:
 *  - native ESP32-S3 FSPI at a fixed 20 MHz;
 *  - ordinary 800x600 controller geometry and ordinary partial windows;
 *  - external one/two-frame A2-style LUT profiles;
 *  - bounded absolute bursts with mandatory settle/DTM1 reseed;
 *  - strict BUSY edge validation and OTP fallback/recovery.
 *
 * The destructive v0.1.6 paths are intentionally absent: no compact TRES/GSST
 * remap, no TCON manipulation, no SPI above 20 MHz, and no undocumented PLL.
 * SSD1677 retains the established bit-banged backend.
 */
#include "RiscDisplayOutputV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include <stddef.h>
#include <stdint.h>

#define FRAME_BYTES ((X4PRO_PANEL_WIDTH / 8u) * X4PRO_PANEL_HEIGHT)
#define ROW_BYTES (X4PRO_PANEL_WIDTH / 8u)
#define UC_CONTROLLER_ROWS 600u
#define UC_VISIBLE_OFFSET 120u
#define UC_LUT_BYTES 42u
#define UC_SPI_CHUNK 64u
#define UC_SPI_HZ 20000000u
#define UC_MIN_BUSY_MS 8u
#define UC_BUSY_ASSERT_MS 100u
#define UC_POWER_TIMEOUT_MS 1500u
#define UC_REFRESH_TIMEOUT_MS 3500u
#define UC_MAX_ABS_FRAMES 8u
#define UC_MAX_ABS_MS 600u
#define UC_BURST_GAP_MS 140u
#define UC_FAST_BEFORE_CLEAN 96u
#define UC_ABS_BEFORE_CLEAN 32u

/* ESP32-S3 SPI2/FSPI register subset. */
#define SYSTEM_PERIP_CLK_EN0_REG 0x600c0018u
#define SYSTEM_PERIP_RST_EN0_REG 0x600c0020u
#define SYSTEM_SPI2_BIT (1u << 6)
#define SPI2_BASE 0x60024000u
#define SPI2_CMD_REG (SPI2_BASE + 0x00u)
#define SPI2_CTRL_REG (SPI2_BASE + 0x08u)
#define SPI2_CLOCK_REG (SPI2_BASE + 0x0cu)
#define SPI2_USER_REG (SPI2_BASE + 0x10u)
#define SPI2_USER1_REG (SPI2_BASE + 0x14u)
#define SPI2_USER2_REG (SPI2_BASE + 0x18u)
#define SPI2_MS_DLEN_REG (SPI2_BASE + 0x1cu)
#define SPI2_MISC_REG (SPI2_BASE + 0x20u)
#define SPI2_W0_REG (SPI2_BASE + 0x98u)
#define SPI2_CMD_USR (1u << 24)
#define SPI2_CMD_UPDATE (1u << 23)
#define SPI2_USER_MOSI (1u << 27)
#define SPI2_CLOCK_20MHZ 0x00003043u /* 80 MHz / (0+1) / (3+1). */

static const risc_platform_clock_api_v1 *clock_api;
static uint8_t frame[FRAME_BYTES];
static uint8_t previous_frame[FRAME_BYTES];
static bool previous_seeded;
static bool partial_update;
static risc_display_rect_v1 update_area;
static uint8_t pending_intent;
static uint64_t transfer_yielded_ms;
static unsigned transfer_work;
static uint8_t present_state;
static bool transfer_started;
static bool started, held, pins_ready;
static uint8_t shutdown_stage;
static int controller;
enum {
    PRESENT_NONE = 0,
    PRESENT_QUEUED = RISC_DISPLAY_PRESENT_QUEUED,
    PRESENT_ACTIVE = RISC_DISPLAY_PRESENT_ACTIVE,
    PRESENT_COMPLETE = RISC_DISPLAY_PRESENT_COMPLETE,
    PRESENT_FAILED = RISC_DISPLAY_PRESENT_FAILED
};
static uint64_t frame_serial, token_serial, pending_token;
static char last_error_text[96];

static uint64_t transfer_start_ms, transfer_end_ms, refresh_ms;
static uint64_t busy_assert_ms, busy_done_ms, wait_start_ms;
static uint32_t bytes_sent, wait_budget_ms;
static uint8_t busy_before;
static const char *reason = "none";
static char probe_text[96] = "probe=not-run";

/* UC8279 state is deliberately distinct from host framebuffer history. */
enum {
    UC_STATE_UNPROBED = 0,
    UC_STATE_UNKNOWN = 1,
    UC_STATE_DIFF_SYNCED = 2,
    UC_STATE_ABS_BURST = 3,
    UC_STATE_FAULTED = 4,
    UC_STATE_SLEEPING = 5
};
enum {
    UC_PROFILE_OTP_CLEAN = 0,
    UC_PROFILE_DIFF_2F = 1,
    UC_PROFILE_DIFF_1F = 2,
    UC_PROFILE_ABS_1F = 3,
    UC_PROFILE_ABS_SETTLE_2F = 4
};
static uint8_t uc_state;
static uint8_t uc_last_profile;
static uint8_t uc_faults;
static bool uc_fast_disabled;
static bool uc_spi_ready;
static bool uc_powered;
static uint32_t uc_fast_updates;
static uint32_t uc_absolute_updates;
static uint32_t uc_burst_frames;
static uint64_t uc_burst_started_ms;
static uint64_t uc_last_present_ms;
static bool uc_abs_dirty_valid;
static risc_display_rect_v1 uc_abs_dirty;

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void fail(const char *text) {
    size_t i = 0;
    if (!text) text = "unknown";
    while (text[i] && i + 1u < sizeof(last_error_text)) {
        last_error_text[i] = text[i];
        ++i;
    }
    last_error_text[i] = 0;
}
static void set_reason(const char *text) { reason = text; fail(text); }
static void sleep_ms(uint32_t ms) {
    if (clock_api && clock_api->sleep_ms) clock_api->sleep_ms(clock_api->context, ms);
}
static uint64_t now_ms(void) {
    if (!clock_api || !clock_api->monotonic_ms) return UINT64_MAX;
    return clock_api->monotonic_ms(clock_api->context);
}
static bool sample_now(uint64_t *out) {
    const uint64_t now = now_ms();
    if (now == UINT64_MAX) { set_reason("clock failure"); return false; }
    if (wait_start_ms && now < wait_start_ms) { set_reason("clock nonmonotonic"); return false; }
    *out = now;
    return true;
}
static void bytes_fill(uint8_t *dst, uint8_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) dst[i] = value;
}
static void bytes_copy(uint8_t *dst, const uint8_t *src, size_t count) {
    for (size_t i = 0; i < count; ++i) dst[i] = src[i];
}
static void copy_rect(uint8_t *dst, const uint8_t *src, const risc_display_rect_v1 *r) {
    const uint32_t x = (uint32_t)r->x / 8u;
    const uint32_t w = r->width / 8u;
    for (uint32_t row = 0; row < r->height; ++row) {
        const size_t offset = (size_t)((uint32_t)r->y + row) * ROW_BYTES + x;
        bytes_copy(dst + offset, src + offset, w);
    }
}
static risc_display_rect_v1 full_visible_rect(void) {
    const risc_display_rect_v1 r = {0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};
    return r;
}
static risc_display_rect_v1 rect_union(risc_display_rect_v1 a, risc_display_rect_v1 b) {
    const uint32_t left = (uint32_t)a.x < (uint32_t)b.x ? (uint32_t)a.x : (uint32_t)b.x;
    const uint32_t top = (uint32_t)a.y < (uint32_t)b.y ? (uint32_t)a.y : (uint32_t)b.y;
    const uint32_t ar = (uint32_t)a.x + a.width;
    const uint32_t br = (uint32_t)b.x + b.width;
    const uint32_t ab = (uint32_t)a.y + a.height;
    const uint32_t bb = (uint32_t)b.y + b.height;
    const uint32_t right = ar > br ? ar : br;
    const uint32_t bottom = ab > bb ? ab : bb;
    const risc_display_rect_v1 r = {(int32_t)left, (int32_t)top, right - left, bottom - top};
    return r;
}

/* ---------------- SSD1677 legacy GPIO transport ---------------- */
static void bb_spi_byte(uint8_t value) {
    for (int bit = 7; bit >= 0; --bit) {
        x4pro_pin_level(X4PRO_PIN_EPD_MOSI, ((value >> bit) & 1u) != 0);
        x4pro_pin_level(X4PRO_PIN_EPD_SCLK, true);
        x4pro_pin_level(X4PRO_PIN_EPD_SCLK, false);
    }
}
static void bb_command(uint8_t cmd) {
    x4pro_pin_level(X4PRO_PIN_EPD_DC, false);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    bb_spi_byte(cmd);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
}
static void bb_data1(uint8_t value) {
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    bb_spi_byte(value);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
}

static bool append(char *destination, size_t capacity, size_t *used, const char *text);
static bool append_u(char *destination, size_t capacity, size_t *used, uint64_t value);
static void append_hex(char *destination, size_t capacity, size_t *used, uint8_t value) {
    const char *digits = "0123456789abcdef";
    if (*used + 2u < capacity) {
        destination[(*used)++] = digits[value >> 4];
        destination[(*used)++] = digits[value & 0x0fu];
        destination[*used] = 0;
    }
}
static void prepare_pins(void) {
    if (pins_ready) return;
    x4pro_pin_input(X4PRO_PIN_EPD_BUSY, false);
    x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
    x4pro_pin_output(X4PRO_PIN_EPD_SCLK, false);
    x4pro_pin_output(X4PRO_PIN_EPD_MOSI, false);
    x4pro_pin_output(X4PRO_PIN_EPD_DC, false);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    x4pro_epd_reset_unhold();
    pins_ready = true;
}
static void clock_delay(void) {
    for (volatile int i = 0; i < 32; ++i) (void)x4pro_pin_read(X4PRO_PIN_EPD_BUSY);
}
static uint8_t read_byte(void) {
    uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        clock_delay();
        value = (uint8_t)((value << 1) | (x4pro_pin_read(X4PRO_PIN_EPD_MOSI) ? 1u : 0u));
        x4pro_pin_level(X4PRO_PIN_EPD_SCLK, true);
        clock_delay();
        x4pro_pin_level(X4PRO_PIN_EPD_SCLK, false);
    }
    return value;
}
static void read_cmd(uint8_t cmd, uint8_t *out, size_t len) {
    x4pro_pin_output(X4PRO_PIN_EPD_MOSI, false);
    x4pro_pin_level(X4PRO_PIN_EPD_DC, false);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    clock_delay();
    bb_spi_byte(cmd);
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_input(X4PRO_PIN_EPD_MOSI, true);
    clock_delay();
    for (size_t i = 0; i < len; ++i) out[i] = read_byte();
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    x4pro_pin_output(X4PRO_PIN_EPD_MOSI, false);
}
enum { PROBE_AMBIGUOUS = 0, PROBE_SSD = 1, PROBE_UC8279 = 2, PROBE_DISABLED = 3 };
__attribute__((weak)) int x4_test_probe_mode = 0;
static int probe_controller(void) {
    uint8_t flg = 0, confirm_flg = 0;
    uint8_t ver[5] = {0}, confirm_ver[5] = {0};
    prepare_pins();
    const uint8_t busy_before_reset = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    x4pro_epd_reset_unhold();
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(1);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(30);
    const uint8_t busy = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    int verdict = PROBE_AMBIGUOUS;
    if (x4_test_probe_mode == 1) verdict = PROBE_DISABLED;
    else if (x4_test_probe_mode == 2) verdict = PROBE_AMBIGUOUS;
    else if (x4_test_probe_mode == 3) { verdict = PROBE_UC8279; ver[2] = 0x68; flg = 0x13; }
    else if (x4_test_probe_mode == 4) verdict = PROBE_SSD;
    else {
        read_cmd(0x71, &flg, 1);
        read_cmd(0x70, ver, sizeof(ver));
        bool floating = true;
        for (size_t i = 1; i < sizeof(ver); ++i) if (ver[i] != ver[0]) floating = false;
        const bool uc = flg != 0 && flg != 0xffu && (flg & 1u) && !floating;
        const bool ssd = floating && (flg == 0 || flg == 0xffu);
        if (uc && ver[2] == 0x68u) {
            sleep_ms(50);
            read_cmd(0x71, &confirm_flg, 1);
            read_cmd(0x70, confirm_ver, sizeof(confirm_ver));
            bool matches = confirm_flg == flg;
            for (size_t i = 0; i < sizeof(ver); ++i) matches = matches && confirm_ver[i] == ver[i];
            verdict = matches && busy ? PROBE_UC8279 : PROBE_AMBIGUOUS;
        } else verdict = ssd ? PROBE_SSD : PROBE_AMBIGUOUS;
    }
    size_t used = 0;
    append(probe_text, sizeof(probe_text), &used, "probe flg=");
    append_hex(probe_text, sizeof(probe_text), &used, flg);
    append(probe_text, sizeof(probe_text), &used, " ver=");
    for (size_t i = 0; i < sizeof(ver); ++i) append_hex(probe_text, sizeof(probe_text), &used, ver[i]);
    append(probe_text, sizeof(probe_text), &used, " busy0=");
    append_u(probe_text, sizeof(probe_text), &used, busy_before_reset);
    append(probe_text, sizeof(probe_text), &used, " busy=");
    append_u(probe_text, sizeof(probe_text), &used, busy);
    append(probe_text, sizeof(probe_text), &used,
           verdict == PROBE_SSD ? " verdict=ssd-assumed" :
           verdict == PROBE_UC8279 ? " verdict=uc8279-68-confirmed" :
           verdict == PROBE_DISABLED ? " verdict=probe-disabled" : " verdict=ambiguous");
    return verdict;
}

