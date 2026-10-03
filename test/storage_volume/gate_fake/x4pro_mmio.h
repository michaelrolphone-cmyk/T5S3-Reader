#pragma once
#include <stdbool.h>
#include <stdint.h>
void x4pro_pin_output(uint32_t pin, bool level);
void x4pro_pin_release(uint32_t pin);
bool x4pro_pin_read(uint32_t pin);
void x4pro_pin_input(uint32_t pin, bool pullup);
void x4pro_pin_hold(uint32_t pin, bool hold);
