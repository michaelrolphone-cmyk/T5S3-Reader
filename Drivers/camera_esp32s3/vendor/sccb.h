#pragma once
#include <stdint.h>
int SCCB_Read16(uint8_t address, uint16_t reg);
int SCCB_Write16(uint8_t address, uint16_t reg, uint8_t value);
