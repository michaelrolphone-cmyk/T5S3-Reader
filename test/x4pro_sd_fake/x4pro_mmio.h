#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "x4pro_pins.h"
extern unsigned x4_sd_fake_ticks;
extern bool x4_sd_fake_bad_pin;
static inline void x4pro_pin_output(uint32_t pin, bool level) {
    (void)level;
    if (pin != X4PRO_PIN_SD_PWR && pin != X4PRO_PIN_SD_CLK && pin != X4PRO_PIN_SD_CMD)
        x4_sd_fake_bad_pin = true;
    if (pin == X4PRO_PIN_SD_CLK) ++x4_sd_fake_ticks;
}
static inline void x4pro_pin_release(uint32_t pin) {
    if (pin != X4PRO_PIN_SD_CMD && pin != X4PRO_PIN_SD_DAT0) x4_sd_fake_bad_pin = true;
}
static inline bool x4pro_pin_read(uint32_t pin) {
    if (pin != X4PRO_PIN_SD_CMD) x4_sd_fake_bad_pin = true;
    return true; /* No card drives CMD low. */
}
