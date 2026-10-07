#pragma once
#include <cstdint>
inline uint32_t ticks = 0;
inline uint32_t millis() { return ticks; }
