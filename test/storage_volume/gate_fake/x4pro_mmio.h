#pragma once
#include <stdbool.h>
#include <stdint.h>
void x4pro_pin_output(uint32_t pin, bool level);
void x4pro_pin_level(uint32_t pin, bool level);
void x4pro_pin_release(uint32_t pin);
bool x4pro_pin_read(uint32_t pin);
void x4pro_pin_input(uint32_t pin, bool pullup);
void x4pro_pin_hold(uint32_t pin, bool hold);

uint32_t x4pro_sd_test_cycle_count(void);
#define X4PRO_SD_CYCLE_COUNT() x4pro_sd_test_cycle_count()
