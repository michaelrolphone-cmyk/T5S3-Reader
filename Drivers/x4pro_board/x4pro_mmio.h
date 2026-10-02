#pragma once
/* ESP32-S3 GPIO MMIO used by X4 Pro bus ELFs. These providers own their pins.
 * They do not import firmware Wire, SPI, or HardwareSerial. */
#include <stdbool.h>
#include <stdint.h>

#define X4PRO_GPIO_BASE 0x60004000u
#define X4PRO_IO_MUX_BASE 0x60009000u

static inline void x4pro_reg_write(uint32_t address, uint32_t value) {
    *(volatile uint32_t *)address = value;
}
static inline uint32_t x4pro_reg_read(uint32_t address) {
    return *(volatile uint32_t *)address;
}
static inline void x4pro_pin_gpio(uint32_t pin) {
    /* MCU_SEL = 1 selects GPIO. Input enable stays on so open-drain reads work. */
    x4pro_reg_write(X4PRO_IO_MUX_BASE + 0x04u + pin * 4u, (1u << 12) | (1u << 9));
}
static inline void x4pro_pin_output(uint32_t pin, bool level) {
    x4pro_pin_gpio(pin);
    x4pro_reg_write(X4PRO_GPIO_BASE + (level ? 0x08u : 0x0cu), 1u << pin);
    x4pro_reg_write(X4PRO_GPIO_BASE + 0x24u, 1u << pin);
}
static inline void x4pro_pin_release(uint32_t pin) {
    x4pro_pin_gpio(pin);
    x4pro_reg_write(X4PRO_GPIO_BASE + 0x28u, 1u << pin);
}
static inline bool x4pro_pin_read(uint32_t pin) {
    return (x4pro_reg_read(X4PRO_GPIO_BASE + 0x3cu) & (1u << pin)) != 0;
}
