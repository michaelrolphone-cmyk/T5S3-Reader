/* T5 frontlight: real ESP32-S3 LEDC timer/channel and GPIO matrix in this ELF.
 * Register fields are from ESP-IDF 4.4 ESP32-S3 ledc_reg.h/system_reg.h and
 * ledc_ll.h. No firmware LEDC calls, device bridge, peripheral-wide reset,
 * or modification of another active timer/channel is permitted.
 * The T5 board profile reserves GPIO11, low-speed timer0 and channel0.
 */
#include "RiscFrontlightV1.h"
#include "x4pro_mmio.h" /* Shared ESP32-S3 GPIO register helpers; no X4 pins. */
#include <stddef.h>
#define LEDC_BASE 0x60019000u
#define SYSTEM_CLOCK 0x600c0018u
#define SYSTEM_RESET 0x600c0020u
#define LEDC_CLOCK_BIT (1u << 11)
#define TIMER0 (LEDC_BASE + 0xa0u)
#define CLOCK_SELECT (LEDC_BASE + 0xd0u)
#define LIGHT_PIN 11u
#define LIGHT_SIGNAL 73u
static bool started, busy;
static uint16_t level;
static bool configure(void) {
    const uint32_t clock = x4pro_reg_read(CLOCK_SELECT) & 3u;
    // APB and XTAL are defined fixed-frequency sources in this profile. Do
    // not guess the calibrated RTC source or retime another active channel.
    if (clock == 2u) return false;
    for (unsigned channel = 1; channel < 8; ++channel) {
        const uint32_t conf = x4pro_reg_read(LEDC_BASE + channel * 0x14u);
        if ((conf & 4u) && (!(conf & 3u) || !clock)) return false;
    }
    x4pro_reg_write(SYSTEM_CLOCK, x4pro_reg_read(SYSTEM_CLOCK) | LEDC_CLOCK_BIT);
    x4pro_reg_write(SYSTEM_RESET, x4pro_reg_read(SYSTEM_RESET) & ~LEDC_CLOCK_BIT);
    if (!clock) x4pro_reg_write(CLOCK_SELECT, (x4pro_reg_read(CLOCK_SELECT) & ~3u) | 3u);
    // Divider has 8 fractional bits: 40/80 MHz / (5000 * 256) * 256.
    const uint32_t divider = clock == 1u ? 16000u : 8000u;
    x4pro_reg_write(TIMER0, (divider << 4) | 8u | (1u << 23));
    x4pro_reg_write(TIMER0, (divider << 4) | 8u | (1u << 25));
    x4pro_reg_write(LEDC_BASE, 1u << 4); // timer0, output disabled, idle low
    x4pro_reg_write(LEDC_BASE + 4u, 0); // hpoint
    x4pro_pin_prepare(LIGHT_PIN, false);
    x4pro_reg_write(X4PRO_GPIO_MATRIX_BASE + LIGHT_PIN * 4u, LIGHT_SIGNAL);
    x4pro_reg_write(x4pro_enable_w1ts(LIGHT_PIN), x4pro_pin_mask(LIGHT_PIN));
    return true;
}
static bool configured(void) {
    const uint32_t clock = x4pro_reg_read(CLOCK_SELECT) & 3u;
    const uint32_t divider = clock == 1u ? 16000u : 8000u;
    // The update bit is self-clearing. Check owned configuration only.
    const uint32_t mask = 0x1ffffffu;
    return (clock == 1u || clock == 3u) &&
        (x4pro_reg_read(TIMER0) & mask) == ((divider << 4) | 8u) &&
        !(x4pro_reg_read(LEDC_BASE) & 3u) &&
        (x4pro_reg_read(X4PRO_GPIO_MATRIX_BASE + LIGHT_PIN * 4u) & 0x1ffu) == LIGHT_SIGNAL;
}
static bool set_level(void *context, uint16_t requested, uint16_t maximum) {
    (void)context;
    if (!maximum || requested > maximum || __atomic_test_and_set(&busy, __ATOMIC_ACQUIRE)) return false;
    if (!started || (!configured() && !configure())) {
        __atomic_clear(&busy, __ATOMIC_RELEASE); return false;
    }
    level = ((uint32_t)requested * 10u + maximum / 2u) / maximum;
    uint32_t duty = (level * level * 255u + 50u) / 100u;
    if (duty == 255u) duty = 256u; // Match Arduino's prior full-on endpoint.
    x4pro_reg_write(LEDC_BASE + 8u, duty << 4);
    // One-step immediate update, increment direction, no fade ISR/task.
    x4pro_reg_write(LEDC_BASE + 0xcu, (1u << 31) | (1u << 30) | (1u << 20) | (1u << 10));
    x4pro_reg_write(LEDC_BASE, (1u << 4) | (duty ? 4u : 0u));
    __atomic_clear(&busy, __ATOMIC_RELEASE);
    return true;
}
static bool get_level(void *context, uint16_t *out, uint16_t *maximum) {
    (void)context;
    if (!started || !out || !maximum) return false;
    *out = level; *maximum = 10; return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    (void)deps;
    if (started || count || !configure()) return false;
    started = true;
    return set_level(NULL, 0, 10);
}
static bool quiesce(void) {
    if (__atomic_test_and_set(&busy, __ATOMIC_ACQUIRE)) return false;
    if (started) {
        // Do not reconfigure the global timer during teardown. Disconnect
        // only our output, force the board light low, then relinquish it.
        x4pro_reg_write(LEDC_BASE, 1u << 4);
        x4pro_pin_output(LIGHT_PIN, false);
    }
    started = false; level = 0;
    __atomic_clear(&busy, __ATOMIC_RELEASE); return true;
}
static void stop(void) { (void)quiesce(); }
static const risc_frontlight_api_v1 api = {1, sizeof(api), NULL, set_level, get_level};
static const risc_driver_v2 driver = {2, sizeof(driver), "t5s3-frontlight",
    "display.frontlight", 1, &api, start, stop, quiesce};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) { return abi == 2 ? &driver : NULL; }
