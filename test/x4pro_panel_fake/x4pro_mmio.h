#pragma once
#include <stdbool.h>
#include <stdint.h>
extern uint8_t x4_fake_busy;
extern uint32_t x4_fake_refresh_count;
extern uint32_t x4_fake_bytes;
static uint8_t x4_fake_shift;
static bool x4_fake_dc;
static bool x4_fake_cs;
extern void x4_fake_on_level(void);
extern uint32_t x4_fake_output_mask;
extern uint32_t x4_fake_level_before_config;
extern uint32_t x4_fake_cmd46;
extern uint32_t x4_fake_cmd47;
extern uint32_t x4_fake_cmd24;
extern uint8_t x4_fake_hold_busy;
static inline void x4pro_pin_level(uint32_t pin, bool level) {
    if (((x4_fake_output_mask >> pin) & 1u) == 0) x4_fake_level_before_config |= 1u << pin;
    if (pin == 11) x4_fake_shift = (uint8_t)((x4_fake_shift << 1) | (level ? 1u : 0u));
    if (pin == 13) {
        if (x4_fake_cs && !level) x4_fake_shift = 0;
        if (!x4_fake_cs && level && !x4_fake_dc) {
            if (x4_fake_shift == 0x20) ++x4_fake_refresh_count;
            if (x4_fake_shift == 0x46) ++x4_fake_cmd46;
            if (x4_fake_shift == 0x47) ++x4_fake_cmd47;
            if (x4_fake_shift == 0x24) ++x4_fake_cmd24;
            if (x4_fake_busy == 4 || (x4_fake_busy == 5 && x4_fake_shift == 0x46)) x4_fake_hold_busy = 1;
        }
        x4_fake_cs = level;
    }
    if (pin == 18) x4_fake_dc = level;
    ++x4_fake_bytes;
    x4_fake_on_level();
    (void)pin;
}
static inline void x4pro_pin_output(uint32_t pin, bool level) {
    x4_fake_output_mask |= 1u << pin;
    x4pro_pin_level(pin, level);
}
static inline void x4pro_pin_input(uint32_t pin, bool pullup) { (void)pin; (void)pullup; }
static inline void x4pro_pin_release(uint32_t pin) { (void)pin; }
static inline bool x4pro_pin_read(uint32_t pin) {
    (void)pin;
    if (x4_fake_hold_busy) return true;
    if (x4_fake_busy == 1 && x4_fake_refresh_count) return true;
    if (x4_fake_busy == 3 && x4_fake_refresh_count) return true;
    return x4_fake_busy == 2;
}
static inline void x4_fake_sleep_hook(void) {
    if (x4_fake_busy == 1 && x4_fake_refresh_count) x4_fake_busy = 0;
    if (x4_fake_busy == 4) x4_fake_hold_busy = 0;
}
