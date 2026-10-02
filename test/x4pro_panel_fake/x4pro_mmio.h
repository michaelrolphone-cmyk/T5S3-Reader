#pragma once
#include <stdbool.h>
#include <stdint.h>
extern uint8_t x4_fake_busy;
extern uint32_t x4_fake_refresh_count;
extern uint32_t x4_fake_bytes;
static uint8_t x4_fake_shift;
static bool x4_fake_dc;
static bool x4_fake_cs;
static inline void x4pro_pin_output(uint32_t pin, bool level) {
    if (pin == 18) x4_fake_dc = level;
    if (pin == 11) x4_fake_shift = (uint8_t)((x4_fake_shift << 1) | (level ? 1u : 0u));
    if (pin == 13) {
        if (x4_fake_cs && !level) x4_fake_shift = 0;
        if (!x4_fake_cs && level && !x4_fake_dc && x4_fake_shift == 0x20) ++x4_fake_refresh_count;
        x4_fake_cs = level;
    }
    ++x4_fake_bytes;
    (void)pin;
}
static inline void x4pro_pin_input(uint32_t pin, bool pullup) { (void)pin; (void)pullup; }
static inline void x4pro_pin_release(uint32_t pin) { (void)pin; }
static inline bool x4pro_pin_read(uint32_t pin) { (void)pin; return x4_fake_busy != 0; }
static inline void x4_fake_sleep_hook(void) {
    if (x4_fake_busy == 1 && x4_fake_refresh_count) x4_fake_busy = 0;
}
