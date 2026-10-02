/* X4 Pro 800x480 panel provider. This artifact is SSD1677-specific.
 * UC8179/UC8279 batches are an unresolved hardware prerequisite.
 * BUSY is active-high. There is no external display PMIC.
 * GPIO1 is the recovered peripheral-enable name and is not driven here.
 * Touch power GPIO2 and SD power GPIO5 stay untouched. */
#include "RiscDisplayOutputV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include <stddef.h>
#include <stdint.h>

#define FRAME_BYTES ((X4PRO_PANEL_WIDTH / 8u) * X4PRO_PANEL_HEIGHT)
static const risc_platform_clock_api_v1 *clock_api;
static uint8_t frame[FRAME_BYTES];
static uint8_t present_state;
static bool transfer_started;
static bool started, held, pins_ready;
enum { PRESENT_NONE = 0, PRESENT_QUEUED = 1, PRESENT_ACTIVE = 2, PRESENT_COMPLETE = 3, PRESENT_FAILED = 5 };
static uint64_t frame_serial, token_serial, pending_token;
static char last_error_text[64];

static bool equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void fail(const char *text) {
    size_t i = 0;
    while (text[i] && i + 1u < sizeof(last_error_text)) { last_error_text[i] = text[i]; ++i; }
    last_error_text[i] = 0;
}
static void sleep_ms(uint32_t ms) {
    if (clock_api && clock_api->sleep_ms) clock_api->sleep_ms(clock_api->context, ms);
}
static void spi_byte(uint8_t value) {
    for (int bit = 7; bit >= 0; --bit) {
        x4pro_pin_output(X4PRO_PIN_EPD_MOSI, (value >> bit) & 1);
        x4pro_pin_output(X4PRO_PIN_EPD_SCLK, true);
        x4pro_pin_output(X4PRO_PIN_EPD_SCLK, false);
    }
}
static void command(uint8_t cmd) {
    x4pro_pin_output(X4PRO_PIN_EPD_DC, false);
    x4pro_pin_output(X4PRO_PIN_EPD_CS, false);
    spi_byte(cmd);
    x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
}
static void data1(uint8_t value) {
    x4pro_pin_output(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_output(X4PRO_PIN_EPD_CS, false);
    spi_byte(value);
    x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
}
static uint64_t now_ms(void) {
    if (!clock_api || !clock_api->monotonic_ms) return UINT64_MAX;
    return clock_api->monotonic_ms(clock_api->context);
}
static bool wait_idle(uint32_t deadline_ms) {
    bool saw_busy = false;
    while (!saw_busy) {
        uint64_t now = now_ms();
        if (now == UINT64_MAX || now >= deadline_ms) { fail("busy never asserted"); return false; }
        saw_busy = x4pro_pin_read(X4PRO_PIN_EPD_BUSY);
        if (!saw_busy) sleep_ms(1);
    }
    while (x4pro_pin_read(X4PRO_PIN_EPD_BUSY)) {
        uint64_t now = now_ms();
        if (now == UINT64_MAX || now >= deadline_ms) { fail("panel busy timeout"); return false; }
        sleep_ms(1);
    }
    return true;
}
static void prepare_pins(void) {
    if (pins_ready) return;
    x4pro_pin_input(X4PRO_PIN_EPD_BUSY, false);
    x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
    x4pro_pin_output(X4PRO_PIN_EPD_SCLK, false);
    x4pro_pin_output(X4PRO_PIN_EPD_DC, false);
    pins_ready = true;
}
static const uint8_t x_window[] = {0x00, 0x00, 0x1F, 0x03};
static const uint8_t y_window[] = {0xDF, 0x01, 0x00, 0x00};
static void set_cursor(void) {
    command(0x4E); data1(0x00); data1(0x00);
    command(0x4F); data1(0xDF); data1(0x01);
}
static bool init_panel(void) {
    static const uint8_t booster[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
    static const uint8_t gate[] = {0xDF, 0x01, 0x02};
    prepare_pins();
    x4pro_pin_output(X4PRO_PIN_EPD_RST, false);
    sleep_ms(10);
    x4pro_pin_output(X4PRO_PIN_EPD_RST, true);
    sleep_ms(10);
    command(0x12);
    sleep_ms(10);
    command(0x18);
    data1(0x80);
    command(0x0C);
    for (size_t i = 0; i < sizeof(booster); ++i) data1(booster[i]);
    command(0x01);
    for (size_t i = 0; i < sizeof(gate); ++i) data1(gate[i]);
    command(0x3C);
    data1(0x80);
    command(0x11);
    data1(0x01);
    command(0x44);
    for (size_t i = 0; i < sizeof(x_window); ++i) data1(x_window[i]);
    command(0x45);
    for (size_t i = 0; i < sizeof(y_window); ++i) data1(y_window[i]);
    set_cursor();
    return true;
}
static bool transfer_frame(uint32_t deadline_ms) {
    if (transfer_started) return false;
    transfer_started = true;
    set_cursor();
    command(0x24);
    x4pro_pin_output(X4PRO_PIN_EPD_DC, true);
    x4pro_pin_output(X4PRO_PIN_EPD_CS, false);
    for (size_t i = 0; i < FRAME_BYTES; ++i) spi_byte((uint8_t)~frame[i]);
    x4pro_pin_output(X4PRO_PIN_EPD_CS, true);
    command(0x21); data1(0x00);
    command(0x22); data1(0xF7); command(0x20);
    return wait_idle(deadline_ms);
}
static bool get_info(void *context, risc_display_info_v1 *out) {
    (void)context;
    if (!out) return false;
    *out = (risc_display_info_v1){0};
    out->api_version = RISC_DISPLAY_OUTPUT_API_V1;
    out->struct_size = sizeof(*out);
    out->width = X4PRO_PANEL_WIDTH;
    out->height = X4PRO_PANEL_HEIGHT;
    out->supported_formats = RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1);
    out->preferred_format = RISC_DISPLAY_FORMAT_MONO1;
    out->supported_rotations = RISC_DISPLAY_ROTATION_0;
    out->flags = RISC_DISPLAY_INFO_RETAINS_IMAGE;
    out->damage_x_alignment = 8;
    out->damage_width_alignment = 8;
    out->damage_y_alignment = 1;
    out->damage_height_alignment = 1;
    out->nominal_refresh_millihz = 500;
    out->typical_present_latency_us = 1600000;
    return true;
}
static bool acquire(void *context, uint32_t format, risc_display_surface_v1 *out) {
    (void)context;
    if (!started || held || !out || format != RISC_DISPLAY_FORMAT_MONO1) return false;
    if (++frame_serial == 0) ++frame_serial;
    held = true;
    *out = (risc_display_surface_v1){frame_serial, frame, X4PRO_PANEL_WIDTH, X4PRO_PANEL_HEIGHT,
        X4PRO_PANEL_WIDTH / 8u, FRAME_BYTES, RISC_DISPLAY_FORMAT_MONO1};
    return true;
}
static void release(void *context, risc_display_frame_v1 frame_id) {
    (void)context;
    if (present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE) return;
    if (held && frame_id == frame_serial) held = false;
}
static bool submit(void *context, risc_display_frame_v1 frame_id, const risc_display_rect_v1 *damage,
                   size_t count, const risc_display_present_options_v1 *options,
                   risc_display_present_token_v1 *token_out) {
    (void)context; (void)damage; (void)count; (void)options;
    if (!started || !held || frame_id != frame_serial || present_state == PRESENT_QUEUED || present_state == PRESENT_ACTIVE)
        return false;
    if (++token_serial == 0) ++token_serial;
    pending_token = token_serial;
    present_state = PRESENT_QUEUED;
    transfer_started = false;
    if (token_out) *token_out = pending_token;
    return true;
}
static bool present_status(void *context, risc_display_present_token_v1 token, risc_display_present_status_v1 *out) {
    (void)context;
    if (!out || token != pending_token) return false;
    out->state = present_state;
    return true;
}
static bool wait_present(void *context, risc_display_present_token_v1 token, uint32_t timeout_ms,
                         risc_display_present_status_v1 *out) {
    if (token != pending_token) return false;
    if (present_state == PRESENT_QUEUED && timeout_ms > 0 && !transfer_started) {
        uint64_t now = now_ms();
        present_state = PRESENT_ACTIVE;
        if (now == UINT64_MAX || !transfer_frame((uint32_t)now + timeout_ms)) {
            present_state = PRESENT_FAILED;
            held = false;
        } else {
            present_state = PRESENT_COMPLETE;
            held = false;
        }
    }
    return present_status(context, token, out);
}
static bool set_brightness(void *context, uint16_t level, uint16_t maximum) {
    (void)context; (void)level; (void)maximum;
    return false;
}
static const risc_display_output_api_v1 api = {
    RISC_DISPLAY_OUTPUT_API_V1, sizeof(api), 0, get_info, acquire, release, submit,
    present_status, wait_present, set_brightness
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    clock_api = 0;
    for (size_t i = 0; i < count; ++i)
        if (equal(dependencies[i].capability_id, "platform.clock") && dependencies[i].api_version == 1)
            clock_api = dependencies[i].api;
    if (!clock_api) { fail("platform.clock missing"); return false; }
    for (size_t i = 0; i < sizeof(frame); ++i) frame[i] = 0;
    started = init_panel();
    return started;
}
static void stop(void) { started = false; held = false; }
static bool quiesce(void) { stop(); return true; }
static bool last_error(char *destination, size_t capacity) {
    if (!destination || !capacity || !last_error_text[0]) return false;
    size_t i = 0;
    while (last_error_text[i] && i + 1u < capacity) { destination[i] = last_error_text[i]; ++i; }
    destination[i] = 0;
    return true;
}
static const risc_driver_diagnostics_v2 driver = {
    { RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_diagnostics_v2), "x4pro-panel",
      "display.output", 1, &api, start, stop, quiesce },
    last_error
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver.base;
}
