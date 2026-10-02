/* X4 Pro side buttons. Owns GPIO 0/3/7. Publishes input.navigation. */
#include "RiscInputNavigationV1.h"
#include "RiscProviderV2.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include <stddef.h>

static uint32_t previous;
static bool started;
static uint32_t sample(void) {
    uint32_t buttons = 0;
    if (!x4pro_pin_read(X4PRO_PIN_BTN_LEFT)) buttons |= RISC_NAV_LEFT;
    if (!x4pro_pin_read(X4PRO_PIN_BTN_RIGHT)) buttons |= RISC_NAV_RIGHT;
    if (!x4pro_pin_read(X4PRO_PIN_BTN_POWER)) buttons |= RISC_NAV_CONFIRM;
    return buttons;
}
static bool poll(void *context, risc_input_navigation_frame_v1 *out) {
    (void)context;
    if (!started || !out) return false;
    uint32_t buttons = sample();
    out->buttons = buttons;
    out->pressed = buttons & ~previous;
    out->released = previous & ~buttons;
    previous = buttons;
    return true;
}
static bool foreground(void *context, const risc_input_foreground_v1 *claims, size_t count) {
    (void)context; (void)claims; (void)count;
    return true;
}
static bool reset(void *context) { (void)context; previous = sample(); return true; }
static const risc_input_navigation_api_v1 api = {
    RISC_INPUT_NAVIGATION_API_V1, sizeof(api), 0, poll, foreground, reset
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    (void)dependencies; (void)count;
    x4pro_pin_input(X4PRO_PIN_BTN_LEFT, true);
    x4pro_pin_input(X4PRO_PIN_BTN_RIGHT, true);
    x4pro_pin_input(X4PRO_PIN_BTN_POWER, true);
    previous = sample();
    started = true;
    return true;
}
static void stop(void) { started = false; }
static bool quiesce(void) { stop(); return true; }
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-buttons",
    "input.navigation", 1, &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver;
}
