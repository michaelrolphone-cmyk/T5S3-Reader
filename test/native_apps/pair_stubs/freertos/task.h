#pragma once
#include <cstdint>
extern uint32_t fakeTime;
extern unsigned pairTestYields;
inline void vTaskDelay(unsigned ticks) { fakeTime += ticks; ++pairTestYields; }
