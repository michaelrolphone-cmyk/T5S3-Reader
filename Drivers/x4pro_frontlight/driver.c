/* X4 Pro frontlight. GPIO 8/9 on-off only. LEDC PWM is not claimed. */
#include "RiscFrontlightV1.h"
#include "x4pro_mmio.h"
#include "x4pro_pins.h"
#include <stddef.h>

static uint16_t level;
static bool started;
static bool set_level(void *context, uint16_t requested, uint16_t maximum) {
    (void)context;
    if (!started || !maximum || requested > maximum) return false;
    level = requested;
    bool on = requested != 0;
    x4pro_pin_output(X4PRO_PIN_LIGHT_COOL, on);
    x4pro_pin_output(X4PRO_PIN_LIGHT_WARM, on);
    return true;
}
static bool get_level(void *context, uint16_t *out, uint16_t *maximum) {
    (void)context;
    if (!started || !out || !maximum) return false;
    *out = level;
    *maximum = 1;
    return true;
}
static void lights_off(void) {
    level = 0;
    x4pro_pin_output(X4PRO_PIN_LIGHT_COOL, false);
    x4pro_pin_output(X4PRO_PIN_LIGHT_WARM, false);
}
static const risc_frontlight_api_v1 api = {
    RISC_FRONTLIGHT_API_V1, sizeof(api), 0, set_level, get_level
};
static bool start(const risc_provider_dependency_v1 *dependencies, size_t count) {
    (void)dependencies; (void)count;
    lights_off();
    started = true;
    return true;
}
static void stop(void) { lights_off(); started = false; }
static bool quiesce(void) { stop(); return true; }
static const risc_driver_v2 driver = {
    RISC_PROVIDER_DRIVER_ABI_V2, sizeof(driver), "x4pro-frontlight",
    "display.frontlight", 1, &api, start, stop, quiesce
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    if (abi != RISC_PROVIDER_DRIVER_ABI_V2) return 0;
    return &driver;
}
