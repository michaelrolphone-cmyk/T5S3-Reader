/*
 * x4pro-panel 0.2.0
 *
 * General-use UC8279 high-refresh engine derived from the hardware-validated
 * X4LAB v0.1.5 mechanism. The SSD1677 implementation is included unchanged
 * from driver.c and remains the selected path for SSD hardware.
 *
 * Safety boundary:
 *   - fixed 800x600 controller geometry, visible rows at +120
 *   - ordinary partial-window commands only
 *   - fixed 20 MHz SPI2 transport
 *   - PLL whitelist 0x0e/0x0f
 *   - no TCON, voltage, PMIC, compact TRES/GSST, 40/80 MHz, or 0x3f PLL paths
 */

#include "RiscDisplayOutputV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include <stddef.h>
#include <stdint.h>

/* Keep the legacy provider implementation in this translation unit so the
 * SSD1677 path and shared ABI helpers remain byte-for-byte unchanged. Hide its
 * exported entry point; this file publishes the 0.2.0 provider below. */
#define get_info legacy_get_info
#define acquire legacy_acquire
#define release legacy_release
#define submit legacy_submit
#define present_status legacy_present_status
#define wait_present legacy_wait_present
#define set_brightness legacy_set_brightness
#define seed_previous legacy_seed_previous
#define start legacy_start
#define stop legacy_stop
#define quiesce legacy_quiesce
#define last_error legacy_last_error
#define api legacy_api
#define driver legacy_driver
#define t5_driver_get legacy_t5_driver_get
#define visibility(value) visibility("hidden")
#include "driver.c"
#undef visibility
#undef t5_driver_get
#undef driver
#undef api
#undef last_error
#undef quiesce
#undef stop
#undef start
#undef seed_previous
#undef set_brightness
#undef wait_present
#undef present_status
#undef submit
#undef release
#undef acquire
#undef get_info

#define UC_SPI2_BASE 0x60024000u
#define UC_SYSTEM_BASE 0x600c0000u
#define UC_SYSTEM_PERIP_CLK_EN0 (UC_SYSTEM_BASE + 0x18u)
#define UC_SYSTEM_PERIP_RST_EN0 (UC_SYSTEM_BASE + 0x20u)
#define UC_SYSTEM_SPI2_BIT (1u << 6)

#define UC_SPI_CMD (UC_SPI2_BASE + 0x00u)
#define UC_SPI_CTRL (UC_SPI2_BASE + 0x08u)
#define UC_SPI_CLOCK (UC_SPI2_BASE + 0x0cu)
#define UC_SPI_USER (UC_SPI2_BASE + 0x10u)
#define UC_SPI_USER1 (UC_SPI2_BASE + 0x14u)
#define UC_SPI_USER2 (UC_SPI2_BASE + 0x18u)
#define UC_SPI_MS_DLEN (UC_SPI2_BASE + 0x1cu)
#define UC_SPI_MISC (UC_SPI2_BASE + 0x20u)
#define UC_SPI_DMA_CONF (UC_SPI2_BASE + 0x30u)
#define UC_SPI_DATA0 (UC_SPI2_BASE + 0x98u)
#define UC_SPI_SLAVE (UC_SPI2_BASE + 0xe0u)
#define UC_SPI_CLK_GATE (UC_SPI2_BASE + 0xe8u)

#define UC_SPI_CMD_UPDATE (1u << 23)
#define UC_SPI_CMD_USR (1u << 24)
#define UC_SPI_USER_MOSI (1u << 27)
#define UC_SPI_FIFO_BYTES 64u
#define UC_SPI_CLOCK_20MHZ 0x00003043u

#define UC_CMD_PSR 0x00u
#define UC_CMD_POF 0x02u
#define UC_CMD_PFS 0x03u
#define UC_CMD_PON 0x04u
#define UC_CMD_DSLP 0x07u
#define UC_CMD_DTM1 0x10u
#define UC_CMD_DRF 0x12u
#define UC_CMD_DTM2 0x13u
#define UC_CMD_PLL 0x30u
#define UC_CMD_CDI 0x50u
#define UC_CMD_TRES 0x61u
#define UC_CMD_GSST 0x65u
#define UC_CMD_PTL 0x90u
#define UC_CMD_PTIN 0x91u
#define UC_CMD_PTOUT 0x92u
#define UC_CMD_CCSET 0xe0u
#define UC_CMD_GATE_SCAN 0xe1u
#define UC_CMD_TSSET 0xe5u

#define UC_PLL_CLEAN 0x0eu
#define UC_PLL_FAST 0x0fu
#define UC_PSR_EXTERNAL 0x37u
#define UC_PSR_OTP 0x17u
#define UC_PSR_1 0x4du
#define UC_PFS_VALUE 0x20u
#define UC_GATE_SCAN_VALUE 0x02u
#define UC_CCSET_VALUE 0x02u
#define UC_TSSET_CLEAN 0x1eu
#define UC_TSSET_FAST 0x5au
#define UC_CDI_CLEAN 0x97u
#define UC_CDI_FAST 0xd7u

#define UC_LUT_BYTES 42u
#define UC_VISIBLE_OFFSET 120u
#define UC_CONTROLLER_ROWS 600u
#define UC_ROW_BYTES (X4PRO_PANEL_WIDTH / 8u)
#define UC_BUSY_ASSERT_MS 40u
#define UC_BUSY_MIN_FAST_MS 2u
#define UC_POWER_TIMEOUT_MS 500u
#define UC_REFRESH_TIMEOUT_MS 1800u
#define UC_ABS_BURST_MAX_FRAMES 16u
#define UC_ABS_BURST_MAX_MS 2000u
#define UC_FAST_CLEAN_LIMIT 256u
#define UC_FAULT_DISABLE_LIMIT 2u

enum {
    UC_STATE_UNKNOWN = 0,
    UC_STATE_CLEAN_SYNCED = 1,
    UC_STATE_DIFF_SYNCED = 2,
    UC_STATE_ABS_BURST = 3,
    UC_STATE_SLEEPING = 4,
    UC_STATE_FAULTED = 5
};

enum {
    UC_PROFILE_OTP_CLEAN = 0,
    UC_PROFILE_DIFF_2F = 1,
    UC_PROFILE_DIFF_1F = 2,
    UC_PROFILE_ABS_1F = 3,
    UC_PROFILE_ABS_2F_SETTLE = 4
};

static uint8_t v020_intent;
static uint8_t uc_state;
static uint8_t uc_profile;
static bool uc_spi_ready;
static bool uc_powered;
static bool uc_dtm1_valid;
static bool uc_fast_disabled;
static bool uc_needs_reinit;
static bool uc_host_shadow_valid;
static uint32_t uc_fault_count;
static uint32_t uc_fast_frames;
static uint32_t uc_absolute_frames;
static uint32_t uc_clean_count;
static uint32_t uc_settle_count;
static uint64_t uc_absolute_started_ms;
static uint64_t uc_last_valid_wave_ms;

static void uc_copy_bytes(uint8_t *destination, const uint8_t *source, size_t count) {
    for (size_t i = 0; i < count; ++i) destination[i] = source[i];
}

static void uc_copy_rect(uint8_t *destination, const uint8_t *source,
                         const risc_display_rect_v1 *rect) {
    const size_t left = (size_t)rect->x / 8u;
    const size_t width = (size_t)rect->width / 8u;
    for (uint32_t row = 0; row < rect->height; ++row) {
        const size_t offset = ((size_t)rect->y + row) * UC_ROW_BYTES + left;
        uc_copy_bytes(destination + offset, source + offset, width);
    }
}

static bool uc_deadline_ok(uint64_t deadline_ms) {
    const uint64_t now = now_ms();
    return now != UINT64_MAX && now < deadline_ms;
}

static bool uc_spi_wait_clear(uint32_t mask, uint64_t deadline_ms, const char *failure) {
    while (x4pro_reg_read(UC_SPI_CMD) & mask) {
        if (!uc_deadline_ok(deadline_ms)) {
            set_reason(failure);
            return false;
        }
    }
    return true;
}

static void uc_spi_route_pin(uint32_t pin) {
    uint32_t mux = x4pro_reg_read(x4pro_iomux_reg(pin));
    mux &= ~((7u << 12) | (1u << 9) | (1u << 8) | (1u << 7));
    mux |= 4u << 12; /* FSPID/FSPICLK native IO_MUX function. */
    x4pro_reg_write(x4pro_iomux_reg(pin), mux);
    x4pro_reg_write(x4pro_enable_w1tc(pin), x4pro_pin_mask(pin));
    x4pro_reg_write(X4PRO_GPIO_MATRIX_BASE + pin * 4u, 0x100u);
}

static bool uc_spi_attach(void) {
    uint32_t value = x4pro_reg_read(UC_SYSTEM_PERIP_CLK_EN0);
    x4pro_reg_write(UC_SYSTEM_PERIP_CLK_EN0, value | UC_SYSTEM_SPI2_BIT);
    value = x4pro_reg_read(UC_SYSTEM_PERIP_RST_EN0);
    x4pro_reg_write(UC_SYSTEM_PERIP_RST_EN0, value | UC_SYSTEM_SPI2_BIT);
    x4pro_reg_write(UC_SYSTEM_PERIP_RST_EN0, value & ~UC_SYSTEM_SPI2_BIT);

    uc_spi_route_pin(X4PRO_PIN_EPD_MOSI);
    uc_spi_route_pin(X4PRO_PIN_EPD_SCLK);

    x4pro_reg_write(UC_SPI_SLAVE, 0u);
    x4pro_reg_write(UC_SPI_USER, 0u);
    x4pro_reg_write(UC_SPI_CTRL, 0u);
    x4pro_reg_write(UC_SPI_USER1, 0u);
    x4pro_reg_write(UC_SPI_USER2, 0u);
    x4pro_reg_write(UC_SPI_DMA_CONF, 0u);
    x4pro_reg_write(UC_SPI_MISC, 0x3fu); /* Disable all hardware CS outputs. */
    x4pro_reg_write(UC_SPI_CLOCK, UC_SPI_CLOCK_20MHZ);
    x4pro_reg_write(UC_SPI_CLK_GATE, 0x7u);
    x4pro_reg_write(UC_SPI_USER, UC_SPI_USER_MOSI);
    x4pro_reg_write(UC_SPI_CMD, UC_SPI_CMD_UPDATE);
    const uint64_t now = now_ms();
    if (now == UINT64_MAX ||
        !uc_spi_wait_clear(UC_SPI_CMD_UPDATE, now + 50u, "spi update timeout")) {
        return false;
    }
    uc_spi_ready = true;
    return true;
}

static bool uc_spi_send_fifo(const uint8_t *data, size_t count, uint64_t deadline_ms) {
    if (!uc_spi_ready || !data || !count || count > UC_SPI_FIFO_BYTES) {
        set_reason("spi invalid transfer");
        return false;
    }
    for (size_t word = 0; word < 16u; ++word) x4pro_reg_write(UC_SPI_DATA0 + word * 4u, 0u);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t address = UC_SPI_DATA0 + (uint32_t)(i / 4u) * 4u;
        uint32_t word = x4pro_reg_read(address);
        word |= (uint32_t)data[i] << ((i & 3u) * 8u);
        x4pro_reg_write(address, word);
    }
    x4pro_reg_write(UC_SPI_MS_DLEN, (uint32_t)(count * 8u - 1u));
    x4pro_reg_write(UC_SPI_CMD, UC_SPI_CMD_UPDATE);
    if (!uc_spi_wait_clear(UC_SPI_CMD_UPDATE, deadline_ms, "spi update timeout")) return false;
    x4pro_reg_write(UC_SPI_CMD, UC_SPI_CMD_USR);
    if (!uc_spi_wait_clear(UC_SPI_CMD_USR, deadline_ms, "spi transfer timeout")) return false;
    return true;
}

static bool uc_spi_send(const uint8_t *data, size_t count, uint64_t deadline_ms) {
    size_t offset = 0;
    while (offset < count) {
        const size_t remaining = count - offset;
        const size_t chunk = remaining > UC_SPI_FIFO_BYTES ? UC_SPI_FIFO_BYTES : remaining;
        if (!uc_spi_send_fifo(data + offset, chunk, deadline_ms)) return false;
        offset += chunk;
        bytes_sent += (uint32_t)chunk;
        if (!transfer_checkpoint(deadline_ms, (unsigned)chunk)) return false;
    }
    return true;
}

static bool uc_command(uint8_t command_value, uint64_t deadline_ms) {
    x4pro_pin_level(X4PRO_PIN_EPD_DC, false);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    const bool ok = uc_spi_send_fifo(&command_value, 1u, deadline_ms);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    return ok;
}

static bool uc_data(const uint8_t *data, size_t count, uint64_t deadline_ms) {
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    const bool ok = uc_spi_send(data, count, deadline_ms);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    return ok;
}

static bool uc_register(uint8_t command_value, const uint8_t *data, size_t count,
                        uint64_t deadline_ms) {
    return uc_command(command_value, deadline_ms) && uc_data(data, count, deadline_ms);
}

static bool uc_register1(uint8_t command_value, uint8_t value, uint64_t deadline_ms) {
    return uc_register(command_value, &value, 1u, deadline_ms);
}

static bool uc_wait_idle_high(uint64_t deadline_ms, const char *failure) {
    while (!x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        if (!uc_deadline_ok(deadline_ms)) {
            set_reason(failure);
            return false;
        }
        sleep_ms(1u);
    }
    return true;
}

static bool uc_wait_busy_cycle(uint64_t deadline_ms, uint32_t minimum_ms,
                               const char *assert_failure, const char *complete_failure) {
    const uint64_t issued = now_ms();
    if (issued == UINT64_MAX) {
        set_reason("clock failure");
        return false;
    }
    uint64_t assert_deadline = issued + UC_BUSY_ASSERT_MS;
    if (assert_deadline > deadline_ms) assert_deadline = deadline_ms;
    while (x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        if (!uc_deadline_ok(assert_deadline)) {
            set_reason(assert_failure);
            return false;
        }
    }
    if (!sample_now(&busy_assert_ms)) return false;
    while (!x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        if (!uc_deadline_ok(deadline_ms)) {
            set_reason(complete_failure);
            return false;
        }
        sleep_ms(1u);
    }
    if (!sample_now(&busy_done_ms)) return false;
    if (busy_done_ms < busy_assert_ms || busy_done_ms - busy_assert_ms < minimum_ms) {
        set_reason("implausible busy duration");
        return false;
    }
    uc_last_valid_wave_ms = busy_done_ms - busy_assert_ms;
    return true;
}

static bool uc_reset_and_init(uint64_t deadline_ms) {
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    x4pro_epd_reset_unhold();
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(50u);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(50u);
    if (!uc_wait_idle_high(deadline_ms, "uc reset busy timeout")) return false;
    if (!uc_spi_ready && !uc_spi_attach()) return false;

    static const uint8_t psr[] = {UC_PSR_EXTERNAL, UC_PSR_1};
    static const uint8_t resolution[] = {0x03u, 0x20u, 0x02u, 0x58u};
    static const uint8_t gate_start[] = {0u, 0u, 0u, 0u};
    if (!uc_register(UC_CMD_PSR, psr, sizeof(psr), deadline_ms) ||
        !uc_register(UC_CMD_TRES, resolution, sizeof(resolution), deadline_ms) ||
        !uc_register(UC_CMD_GSST, gate_start, sizeof(gate_start), deadline_ms) ||
        !uc_register1(UC_CMD_PFS, UC_PFS_VALUE, deadline_ms) ||
        !uc_register1(UC_CMD_PLL, UC_PLL_CLEAN, deadline_ms) ||
        !uc_register1(UC_CMD_GATE_SCAN, UC_GATE_SCAN_VALUE, deadline_ms)) {
        return false;
    }
    uc_powered = false;
    uc_dtm1_valid = false;
    uc_state = UC_STATE_UNKNOWN;
    return true;
}

static bool uc_power_on(uint64_t deadline_ms) {
    if (uc_powered) return true;
    if (!uc_command(UC_CMD_PON, deadline_ms) ||
        !uc_wait_busy_cycle(deadline_ms, 0u, "power busy never asserted",
                            "power busy timeout")) {
        return false;
    }
    uc_powered = true;
    return true;
}

static bool uc_set_window(const risc_display_rect_v1 *rect, uint64_t deadline_ms) {
    const uint16_t left = (uint16_t)rect->x;
    const uint16_t right = (uint16_t)(rect->x + (int32_t)rect->width - 1);
    const uint16_t top = (uint16_t)rect->y + UC_VISIBLE_OFFSET;
    const uint16_t bottom = top + (uint16_t)rect->height - 1u;
    const uint8_t window[] = {
        (uint8_t)(left >> 8), (uint8_t)(left & 0xf8u),
        (uint8_t)(right >> 8), (uint8_t)(right | 0x07u),
        (uint8_t)(top >> 8), (uint8_t)top,
        (uint8_t)(bottom >> 8), (uint8_t)bottom, 0x01u
    };
    return uc_command(UC_CMD_PTIN, deadline_ms) &&
           uc_register(UC_CMD_PTL, window, sizeof(window), deadline_ms);
}

static bool uc_stream_region(uint8_t ram_command, const uint8_t *pixels,
                             const risc_display_rect_v1 *rect, uint64_t deadline_ms) {
    if (!uc_set_window(rect, deadline_ms) || !uc_command(ram_command, deadline_ms)) return false;
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    const size_t left = (size_t)rect->x / 8u;
    const size_t width = (size_t)rect->width / 8u;
    uint8_t buffer[UC_SPI_FIFO_BYTES];
    size_t buffered = 0;
    bool ok = true;
    for (uint32_t row = 0; row < rect->height && ok; ++row) {
        const size_t row_offset = ((size_t)rect->y + row) * UC_ROW_BYTES + left;
        for (size_t col = 0; col < width; ++col) {
            buffer[buffered++] = (uint8_t)~pixels[row_offset + col];
            if (buffered == sizeof(buffer)) {
                ok = uc_spi_send(buffer, buffered, deadline_ms);
                buffered = 0;
            }
        }
    }
    if (ok && buffered) ok = uc_spi_send(buffer, buffered, deadline_ms);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    if (!ok) return false;
    return uc_command(UC_CMD_PTOUT, deadline_ms);
}

static bool uc_stream_full_stride(uint8_t ram_command, const uint8_t *pixels,
                                  bool white, uint64_t deadline_ms) {
    if (!uc_command(ram_command, deadline_ms)) return false;
    x4pro_pin_level(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, false);
    uint8_t buffer[UC_SPI_FIFO_BYTES];
    size_t buffered = 0;
    bool ok = true;
    for (uint32_t row = 0; row < UC_CONTROLLER_ROWS && ok; ++row) {
        for (size_t col = 0; col < UC_ROW_BYTES; ++col) {
            uint8_t value = 0xffu;
            if (!white && row >= UC_VISIBLE_OFFSET) {
                value = (uint8_t)~pixels[(row - UC_VISIBLE_OFFSET) * UC_ROW_BYTES + col];
            }
            buffer[buffered++] = value;
            if (buffered == sizeof(buffer)) {
                ok = uc_spi_send(buffer, buffered, deadline_ms);
                buffered = 0;
            }
        }
    }
    if (ok && buffered) ok = uc_spi_send(buffer, buffered, deadline_ms);
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    return ok;
}

static void uc_build_lut(uint8_t out[5][UC_LUT_BYTES], uint8_t frames, bool absolute) {
    for (size_t table = 0; table < 5u; ++table)
        for (size_t i = 0; i < UC_LUT_BYTES; ++i) out[table][i] = 0u;
    for (size_t table = 0; table < 5u; ++table) {
        out[table][0] = 0x01u;
        out[table][5] = 0x01u;
        out[table][6] = 0x01u;
    }
    out[0][1] = frames;
    if (absolute) {
        out[1][1] = (uint8_t)(0x40u | frames);
        out[2][1] = (uint8_t)(0x80u | frames);
        out[3][1] = (uint8_t)(0x40u | frames);
        out[4][1] = (uint8_t)(0x80u | frames);
    } else {
        out[1][1] = frames;
        out[4][1] = frames;
        out[2][1] = (uint8_t)(0x80u | frames);
        out[3][1] = (uint8_t)(0x40u | frames);
    }
}

static bool uc_upload_lut(uint8_t frames, bool absolute, uint64_t deadline_ms) {
    static const uint8_t registers[5] = {0x20u, 0x21u, 0x22u, 0x23u, 0x24u};
    uint8_t lut[5][UC_LUT_BYTES];
    uc_build_lut(lut, frames, absolute);
    for (size_t table = 0; table < 5u; ++table)
        if (!uc_register(registers[table], lut[table], UC_LUT_BYTES, deadline_ms)) return false;
    return true;
}

static bool uc_trigger_fast(const risc_display_rect_v1 *rect, uint8_t frames,
                            bool absolute, uint64_t deadline_ms) {
    static const uint8_t psr[] = {UC_PSR_EXTERNAL, UC_PSR_1};
    if (!uc_register1(UC_CMD_PLL, UC_PLL_FAST, deadline_ms) ||
        !uc_set_window(rect, deadline_ms) ||
        !uc_register(UC_CMD_PSR, psr, sizeof(psr), deadline_ms) ||
        !uc_register1(UC_CMD_PFS, UC_PFS_VALUE, deadline_ms) ||
        !uc_register1(UC_CMD_GATE_SCAN, UC_GATE_SCAN_VALUE, deadline_ms) ||
        !uc_register1(UC_CMD_CDI, UC_CDI_FAST, deadline_ms) ||
        !uc_register1(UC_CMD_CCSET, UC_CCSET_VALUE, deadline_ms) ||
        !uc_register1(UC_CMD_TSSET, UC_TSSET_FAST, deadline_ms) ||
        !uc_upload_lut(frames, absolute, deadline_ms) ||
        !uc_power_on(deadline_ms)) {
        return false;
    }
    busy_before = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    if (!busy_before) {
        set_reason("busy already active");
        return false;
    }
    if (!sample_now(&refresh_ms) || !uc_command(UC_CMD_DRF, deadline_ms)) return false;
    if (!uc_wait_busy_cycle(deadline_ms, UC_BUSY_MIN_FAST_MS,
                            "busy never asserted", "busy completion timeout")) {
        return false;
    }
    return uc_command(UC_CMD_PTOUT, deadline_ms);
}

static bool uc_clean_to_target(const uint8_t *pixels, uint64_t deadline_ms) {
    static const uint8_t psr[] = {UC_PSR_OTP, UC_PSR_1};
    if (!uc_register1(UC_CMD_PLL, UC_PLL_CLEAN, deadline_ms) ||
        !uc_stream_full_stride(UC_CMD_DTM1, pixels, true, deadline_ms) ||
        !uc_stream_full_stride(UC_CMD_DTM2, pixels, false, deadline_ms) ||
        !uc_register1(UC_CMD_CDI, UC_CDI_CLEAN, deadline_ms) ||
        !uc_register1(UC_CMD_CCSET, UC_CCSET_VALUE, deadline_ms) ||
        !uc_register1(UC_CMD_TSSET, UC_TSSET_CLEAN, deadline_ms) ||
        !uc_power_on(deadline_ms) ||
        !uc_register(UC_CMD_PSR, psr, sizeof(psr), deadline_ms)) {
        return false;
    }
    busy_before = x4pro_pin_read(X4PRO_PIN_EPD_BUSY) ? 1u : 0u;
    if (!busy_before) {
        set_reason("busy already active");
        return false;
    }
    if (!sample_now(&refresh_ms) || !uc_command(UC_CMD_DRF, deadline_ms)) return false;
    if (!uc_wait_busy_cycle(deadline_ms, 20u, "clean busy never asserted",
                            "clean busy completion timeout")) {
        return false;
    }
    if (!uc_stream_full_stride(UC_CMD_DTM1, pixels, false, deadline_ms)) return false;
    uc_dtm1_valid = true;
    uc_state = UC_STATE_CLEAN_SYNCED;
    uc_fast_frames = 0u;
    uc_absolute_frames = 0u;
    ++uc_clean_count;
    return true;
}

static bool uc_sync_dtm1_full(const uint8_t *pixels, uint64_t deadline_ms) {
    const risc_display_rect_v1 full = {0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};
    if (!uc_stream_region(UC_CMD_DTM1, pixels, &full, deadline_ms)) return false;
    uc_dtm1_valid = true;
    return true;
}

static bool uc_present_profile(const uint8_t *pixels, const risc_display_rect_v1 *rect,
                               uint8_t profile, uint64_t deadline_ms) {
    transfer_started = true;
    if (uc_needs_reinit) {
        if (!uc_reset_and_init(deadline_ms)) return false;
        uc_needs_reinit = false;
    }
    if (!sample_now(&transfer_start_ms) ||
        !uc_wait_idle_high(deadline_ms, "uc pre-transfer busy")) {
        return false;
    }
    if (profile == UC_PROFILE_OTP_CLEAN) {
        const bool ok = uc_clean_to_target(pixels, deadline_ms);
        if (ok) transfer_end_ms = refresh_ms;
        return ok;
    }

    const bool absolute = profile == UC_PROFILE_ABS_1F ||
                          profile == UC_PROFILE_ABS_2F_SETTLE;
    const uint8_t frames = (profile == UC_PROFILE_DIFF_2F ||
                            profile == UC_PROFILE_ABS_2F_SETTLE) ? 2u : 1u;

    if (!absolute && !uc_dtm1_valid) {
        set_reason("dtm1 not synchronized");
        return false;
    }
    if (!uc_stream_region(UC_CMD_DTM2, pixels, rect, deadline_ms) ||
        !sample_now(&transfer_end_ms) ||
        !uc_trigger_fast(rect, frames, absolute, deadline_ms)) {
        return false;
    }
    if (absolute) {
        uc_dtm1_valid = false;
        if (profile == UC_PROFILE_ABS_2F_SETTLE) {
            if (!uc_sync_dtm1_full(pixels, deadline_ms)) return false;
            uc_state = UC_STATE_DIFF_SYNCED;
            uc_absolute_frames = 0u;
            ++uc_settle_count;
        } else {
            if (uc_state != UC_STATE_ABS_BURST) {
                uc_absolute_started_ms = now_ms();
                uc_absolute_frames = 0u;
            }
            ++uc_absolute_frames;
            uc_state = UC_STATE_ABS_BURST;
        }
    } else {
        if (!uc_stream_region(UC_CMD_DTM1, pixels, rect, deadline_ms)) return false;
        uc_dtm1_valid = true;
        uc_state = UC_STATE_DIFF_SYNCED;
        uc_absolute_frames = 0u;
    }
    ++uc_fast_frames;
    return true;
}

static uint8_t uc_choose_profile(uint8_t intent, uint64_t now) {
    if (uc_fast_disabled || uc_state == UC_STATE_UNKNOWN ||
        uc_state == UC_STATE_FAULTED || intent == RISC_DISPLAY_PRESENT_CLEAN ||
        uc_fast_frames >= UC_FAST_CLEAN_LIMIT) {
        return UC_PROFILE_OTP_CLEAN;
    }
    if (uc_state == UC_STATE_ABS_BURST) {
        const bool within_frames = uc_absolute_frames < UC_ABS_BURST_MAX_FRAMES;
        const bool within_time = uc_absolute_started_ms != UINT64_MAX &&
                                 now >= uc_absolute_started_ms &&
                                 now - uc_absolute_started_ms < UC_ABS_BURST_MAX_MS;
        if (intent == RISC_DISPLAY_PRESENT_LOW_LATENCY && within_frames && within_time)
            return UC_PROFILE_ABS_1F;
        return UC_PROFILE_ABS_2F_SETTLE;
    }
    if (intent == RISC_DISPLAY_PRESENT_LOW_LATENCY) return UC_PROFILE_ABS_1F;
    if (intent == RISC_DISPLAY_PRESENT_QUALITY) return UC_PROFILE_DIFF_2F;
    return UC_PROFILE_DIFF_1F;
}

static void uc_mark_fault(const char *failure) {
    set_reason(failure);
    uc_state = UC_STATE_FAULTED;
    uc_needs_reinit = true;
    uc_dtm1_valid = false;
    ++uc_fault_count;
    if (uc_fault_count >= UC_FAULT_DISABLE_LIMIT) uc_fast_disabled = true;
    x4pro_pin_level(X4PRO_PIN_EPD_CS, true);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(20u);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    uc_powered = false;
}

static bool v020_get_info(void *context, risc_display_info_v1 *out) {
    if (!legacy_get_info(context, out)) return false;
    if (controller == PROBE_UC8279) {
        out->flags |= RISC_DISPLAY_INFO_CLEAN_PRESENT;
        out->nominal_refresh_millihz = 10000u;
        out->typical_present_latency_us = 100000u;
    }
    return true;
}

static bool v020_submit(void *context, risc_display_frame_v1 frame_id,
                        const risc_display_rect_v1 *damage, size_t count,
                        const risc_display_present_options_v1 *options,
                        risc_display_present_token_v1 *token_out) {
    if (controller != PROBE_UC8279)
        return legacy_submit(context, frame_id, damage, count, options, token_out);
    (void)context;
    if (!started || shutdown_stage || !held || frame_id != frame_serial ||
        present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return false;
    if (!token_out || count > RISC_DISPLAY_MAX_DAMAGE_RECTS || (count && !damage)) return false;

    update_area = (risc_display_rect_v1){0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};
    if (count) {
        uint32_t left = X4PRO_PANEL_WIDTH, top = X4PRO_PANEL_HEIGHT, right = 0u, bottom = 0u;
        for (size_t i = 0; i < count; ++i) {
            const risc_display_rect_v1 *rect = &damage[i];
            if (rect->x < 0 || rect->y < 0 || !rect->width || !rect->height ||
                rect->width > X4PRO_PANEL_WIDTH || rect->height > X4PRO_PANEL_HEIGHT ||
                (uint32_t)rect->x > X4PRO_PANEL_WIDTH - rect->width ||
                (uint32_t)rect->y > X4PRO_PANEL_HEIGHT - rect->height) return false;
            if ((uint32_t)rect->x < left) left = (uint32_t)rect->x;
            if ((uint32_t)rect->y < top) top = (uint32_t)rect->y;
            if ((uint32_t)rect->x + rect->width > right) right = (uint32_t)rect->x + rect->width;
            if ((uint32_t)rect->y + rect->height > bottom) bottom = (uint32_t)rect->y + rect->height;
        }
        left &= ~7u;
        right = (right + 7u) & ~7u;
        if (right > X4PRO_PANEL_WIDTH) right = X4PRO_PANEL_WIDTH;
        update_area = (risc_display_rect_v1){(int32_t)left, (int32_t)top,
                                             right - left, bottom - top};
    }
    v020_intent = options ? options->intent : RISC_DISPLAY_PRESENT_DEFAULT;
    partial_update = count != 0u && v020_intent != RISC_DISPLAY_PRESENT_CLEAN;
    if (++token_serial == 0u) ++token_serial;
    pending_token = token_serial;
    present_state = PRESENT_QUEUED;
    transfer_started = false;
    previous_seeded = false;
    *token_out = pending_token;
    return true;
}

static bool v020_wait_present(void *context, risc_display_present_token_v1 token,
                              uint32_t timeout_ms, risc_display_present_status_v1 *out) {
    if (controller != PROBE_UC8279)
        return legacy_wait_present(context, token, timeout_ms, out);
    if (token != pending_token) return false;
    if (present_state == PRESENT_QUEUED && timeout_ms > 0u && !transfer_started) {
        const uint64_t now = now_ms();
        wait_budget_ms = timeout_ms;
        bytes_sent = 0u;
        refresh_ms = busy_assert_ms = busy_done_ms = transfer_start_ms = transfer_end_ms = 0u;
        wait_start_ms = transfer_yielded_ms = now;
        transfer_work = 0u;
        reason = "none";
        present_state = PRESENT_ACTIVE;
        if (now == UINT64_MAX || timeout_ms > UINT64_MAX - now) {
            uc_mark_fault("clock failure");
            present_state = PRESENT_FAILED;
        } else {
            const uint64_t deadline = now + timeout_ms;
            uc_profile = uc_choose_profile(v020_intent, now);
            if (!uc_present_profile(frame, &update_area, uc_profile, deadline)) {
                uc_mark_fault(reason[0] == 'n' ? "uc present failed" : reason);
                present_state = PRESENT_FAILED;
            } else {
                uc_copy_rect(previous_frame, frame, &update_area);
                if (uc_profile == UC_PROFILE_OTP_CLEAN)
                    uc_copy_bytes(previous_frame, frame, FRAME_BYTES);
                uc_host_shadow_valid = true;
                previous_seeded = true;
                present_state = PRESENT_COMPLETE;
                reason = "complete";
            }
        }
        held = false;
    }
    return legacy_present_status(context, token, out);
}

static bool v020_seed_previous(void *context, risc_display_frame_v1 frame_id) {
    const bool ok = legacy_seed_previous(context, frame_id);
    if (ok && controller == PROBE_UC8279) {
        uc_host_shadow_valid = true;
        uc_dtm1_valid = false;
        uc_state = UC_STATE_UNKNOWN;
    }
    return ok;
}

static bool v020_start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    shutdown_stage = 0u;
    previous_seeded = partial_update = false;
    clock_api = 0;
    for (size_t i = 0; i < count; ++i)
        if (equal(dependencies[i].capability_id, "platform.clock") &&
            dependencies[i].api_version == 1u)
            clock_api = dependencies[i].api;
    if (!clock_api) {
        fail("platform.clock missing");
        return false;
    }
    prepare_pins();
    const int verdict = probe_controller();
    if (verdict != PROBE_SSD && verdict != PROBE_UC8279) {
        set_reason(verdict == PROBE_DISABLED ? "probe-disabled" : "ambiguous-controller");
        return false;
    }
    controller = verdict;
    for (size_t i = 0; i < FRAME_BYTES; ++i) {
        frame[i] = 0u;
        previous_frame[i] = 0u;
    }
    if (controller == PROBE_SSD) {
        started = init_panel();
        return started;
    }

    uc_spi_ready = false;
    uc_powered = false;
    uc_dtm1_valid = false;
    uc_fast_disabled = false;
    uc_needs_reinit = false;
    uc_host_shadow_valid = false;
    uc_fault_count = 0u;
    uc_fast_frames = 0u;
    uc_absolute_frames = 0u;
    uc_clean_count = 0u;
    uc_settle_count = 0u;
    uc_absolute_started_ms = UINT64_MAX;
    uc_last_valid_wave_ms = 0u;
    uc_state = UC_STATE_UNKNOWN;
    reason = "none";

    const uint64_t now = now_ms();
    started = now != UINT64_MAX && uc_spi_attach() &&
              uc_reset_and_init(now + 1000u);
    if (!started) uc_mark_fault("uc init failed");
    return started;
}

static void v020_stop(void) {
    started = false;
    held = false;
    if (controller == PROBE_UC8279) {
        uc_state = UC_STATE_UNKNOWN;
        uc_dtm1_valid = false;
    }
}

static bool v020_quiesce(void) {
    if (controller != PROBE_UC8279) return legacy_quiesce();
    if (held || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return false;
    if (!started || shutdown_stage == 3u) return true;
    const uint64_t began = now_ms();
    if (began == UINT64_MAX || began > UINT64_MAX - 1800u) return false;
    const uint64_t deadline = began + 1800u;

    if (uc_state == UC_STATE_ABS_BURST && uc_host_shadow_valid) {
        const risc_display_rect_v1 full = {0, 0, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT};
        if (!uc_stream_region(UC_CMD_DTM2, previous_frame, &full, deadline) ||
            !uc_trigger_fast(&full, 2u, true, deadline) ||
            !uc_sync_dtm1_full(previous_frame, deadline)) {
            uc_mark_fault("quiesce settle failed");
            return false;
        }
        uc_state = UC_STATE_DIFF_SYNCED;
        ++uc_settle_count;
    }
    if (!uc_wait_idle_high(deadline, "quiesce busy")) return false;
    if (!uc_command(UC_CMD_POF, deadline) ||
        !uc_wait_busy_cycle(deadline, 0u, "power-off busy never asserted",
                            "power-off timeout") ||
        !uc_command(UC_CMD_DSLP, deadline)) {
        uc_mark_fault("quiesce power failed");
        return false;
    }
    const uint8_t check = 0xa5u;
    if (!uc_data(&check, 1u, deadline)) {
        uc_mark_fault("deep-sleep data failed");
        return false;
    }
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    x4pro_pin_hold(X4PRO_PIN_EPD_RST, true);
    shutdown_stage = 3u;
    uc_state = UC_STATE_SLEEPING;
    uc_dtm1_valid = false;
    uc_powered = false;
    started = false;
    return true;
}

static const char *uc_state_name(void) {
    switch (uc_state) {
        case UC_STATE_CLEAN_SYNCED: return "clean";
        case UC_STATE_DIFF_SYNCED: return "diff";
        case UC_STATE_ABS_BURST: return "abs";
        case UC_STATE_SLEEPING: return "sleep";
        case UC_STATE_FAULTED: return "fault";
        default: return "unknown";
    }
}

static const char *uc_profile_name(void) {
    switch (uc_profile) {
        case UC_PROFILE_DIFF_2F: return "diff2";
        case UC_PROFILE_DIFF_1F: return "diff1";
        case UC_PROFILE_ABS_1F: return "abs1";
        case UC_PROFILE_ABS_2F_SETTLE: return "settle2";
        default: return "otp";
    }
}

static bool v020_last_error(char *destination, size_t capacity) {
    if (!destination || !capacity) return false;
    size_t used = 0u;
    destination[0] = 0;
    append(destination, capacity, &used, probe_text);
    append(destination, capacity, &used, " v=0.2.0 state=");
    append(destination, capacity, &used, controller == PROBE_UC8279 ? uc_state_name() : "ssd");
    append(destination, capacity, &used, " profile=");
    append(destination, capacity, &used, controller == PROBE_UC8279 ? uc_profile_name() : "legacy");
    append(destination, capacity, &used, " reason=");
    append(destination, capacity, &used, reason);
    append(destination, capacity, &used, " fast_off=");
    append_u(destination, capacity, &used, uc_fast_disabled ? 1u : 0u);
    append(destination, capacity, &used, " faults=");
    append_u(destination, capacity, &used, uc_fault_count);
    append(destination, capacity, &used, " burst=");
    append_u(destination, capacity, &used, uc_absolute_frames);
    append(destination, capacity, &used, " fast=");
    append_u(destination, capacity, &used, uc_fast_frames);
    append(destination, capacity, &used, " clean=");
    append_u(destination, capacity, &used, uc_clean_count);
    append(destination, capacity, &used, " settle=");
    append_u(destination, capacity, &used, uc_settle_count);
    append(destination, capacity, &used, " bytes=");
    append_u(destination, capacity, &used, bytes_sent);
    append(destination, capacity, &used, " wave_ms=");
    append_u(destination, capacity, &used, uc_last_valid_wave_ms);
    return used > 0u;
}

static const risc_display_output_api_v1_history v020_api = {
    {RISC_DISPLAY_OUTPUT_API_V1, sizeof(v020_api), 0,
     v020_get_info, legacy_acquire, legacy_release, v020_submit,
     legacy_present_status, v020_wait_present, legacy_set_brightness},
    RISC_DISPLAY_HISTORY_TAG, 1u, v020_seed_previous
};

static const risc_driver_diagnostics_v2 v020_driver = {
    {RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2),
     "x4pro-panel", "display.output", 1u, &v020_api,
     v020_start, v020_stop, v020_quiesce},
    v020_last_error
};

__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &v020_driver.base;
}
