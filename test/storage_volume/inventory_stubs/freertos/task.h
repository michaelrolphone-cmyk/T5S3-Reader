#pragma once
#include <Arduino.h>
inline void vTaskDelay(unsigned n) {delay(n);}
inline uint32_t xTaskGetTickCount(){return millis();}
