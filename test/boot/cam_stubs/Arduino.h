#pragma once
#include "Print.h"
using TaskHandle_t = void*;
namespace Fake { inline TaskHandle_t task=reinterpret_cast<void*>(1); inline uint32_t time=0; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return Fake::task; }
inline void vTaskDelay(unsigned n) { Fake::time+=n; }
inline void delay(unsigned n) { vTaskDelay(n); }
inline uint32_t millis() { return Fake::time; }
