#pragma once
#include <cstdint>
extern uint32_t testMillis;
extern unsigned testYields;
inline uint32_t millis() { return testMillis; }
inline void vTaskDelay(unsigned ticks) { testMillis += ticks; ++testYields; }
