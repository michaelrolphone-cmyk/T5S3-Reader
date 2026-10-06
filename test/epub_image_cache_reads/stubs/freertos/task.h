#pragma once
#include <Arduino.h>
inline uint64_t rowYields=0;
inline void vTaskDelay(unsigned ticks){rowYields+=ticks;recordYield(ticks);}
