#pragma once
#include <Arduino.h>
inline void vTaskDelay(unsigned ticks) { delay(ticks); }
inline uint32_t xTaskGetTickCount() { return millis(); }
