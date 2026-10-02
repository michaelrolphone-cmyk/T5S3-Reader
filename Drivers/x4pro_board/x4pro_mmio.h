#pragma once
/* ESP32-S3 GPIO access for X4 Pro providers. Pins 0-31 use bank 0.
 * Pins 32-48 use bank 1 with a pin-32 bit index. IO_MUX is configured once. */
#include <stdbool.h>
#include <stdint.h>

#define X4PRO_GPIO_BASE 0x60004000u
#define X4PRO_IO_MUX_BASE 0x60009000u
#define X4PRO_GPIO_MATRIX_BASE 0x60004554u
#define X4PRO_GPIO_PIN_MAX 48u

static inline bool x4pro_pin_valid(uint32_t pin) { return pin <= X4PRO_GPIO_PIN_MAX; }
static inline bool x4pro_pin_high_bank(uint32_t pin) { return pin >= 32u; }
static inline uint32_t x4pro_pin_bit(uint32_t pin) {
    return x4pro_pin_high_bank(pin) ? pin - 32u : pin;
}
static inline uint32_t x4pro_pin_mask(uint32_t pin) { return 1u << x4pro_pin_bit(pin); }
static inline uint32_t x4pro_out_w1ts(uint32_t pin) {
    return X4PRO_GPIO_BASE + (x4pro_pin_high_bank(pin) ? 0x14u : 0x08u);
}
static inline uint32_t x4pro_out_w1tc(uint32_t pin) {
    return X4PRO_GPIO_BASE + (x4pro_pin_high_bank(pin) ? 0x18u : 0x0cu);
}
static inline uint32_t x4pro_enable_w1ts(uint32_t pin) {
    return X4PRO_GPIO_BASE + (x4pro_pin_high_bank(pin) ? 0x30u : 0x24u);
}
static inline uint32_t x4pro_enable_w1tc(uint32_t pin) {
    return X4PRO_GPIO_BASE + (x4pro_pin_high_bank(pin) ? 0x34u : 0x28u);
}
static inline uint32_t x4pro_in_reg(uint32_t pin) {
    return X4PRO_GPIO_BASE + (x4pro_pin_high_bank(pin) ? 0x40u : 0x3cu);
}
static inline uint32_t x4pro_iomux_reg(uint32_t pin) { return X4PRO_IO_MUX_BASE + 0x04u + pin * 4u; }

static inline void x4pro_reg_write(uint32_t address, uint32_t value) {
    *(volatile uint32_t *)address = value;
}
static inline uint32_t x4pro_reg_read(uint32_t address) {
    return *(volatile uint32_t *)address;
}
static inline void x4pro_pin_prepare(uint32_t pin, bool pullup) {
    if (!x4pro_pin_valid(pin) || pin == 19u || pin == 20u) return;
    uint32_t mux = x4pro_reg_read(x4pro_iomux_reg(pin));
    mux &= ~((7u << 12) | (1u << 9) | (1u << 8) | (1u << 7));
    mux |= (1u << 12) | (1u << 9);
    if (pullup) mux |= 1u << 8;
    x4pro_reg_write(x4pro_iomux_reg(pin), mux);
    x4pro_reg_write(X4PRO_GPIO_MATRIX_BASE + pin * 4u, 0x100u);
}
static inline void x4pro_pin_output(uint32_t pin, bool level) {
    if (!x4pro_pin_valid(pin)) return;
    x4pro_pin_prepare(pin, false);
    x4pro_reg_write(level ? x4pro_out_w1ts(pin) : x4pro_out_w1tc(pin), x4pro_pin_mask(pin));
    x4pro_reg_write(x4pro_enable_w1ts(pin), x4pro_pin_mask(pin));
}
static inline void x4pro_pin_input(uint32_t pin, bool pullup) {
    if (!x4pro_pin_valid(pin)) return;
    x4pro_pin_prepare(pin, pullup);
    x4pro_reg_write(x4pro_enable_w1tc(pin), x4pro_pin_mask(pin));
}
static inline void x4pro_pin_release(uint32_t pin) { x4pro_pin_input(pin, true); }
static inline bool x4pro_pin_read(uint32_t pin) {
    if (!x4pro_pin_valid(pin)) return true;
    return (x4pro_reg_read(x4pro_in_reg(pin)) & x4pro_pin_mask(pin)) != 0;
}
