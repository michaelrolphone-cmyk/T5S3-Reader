#pragma once

#include "FreeRTOS.h"

#include <cstddef>

extern TickType_t native_storage_test_ticks;
extern size_t native_storage_test_yield_count;

inline TickType_t xTaskGetTickCount() { return native_storage_test_ticks; }
inline void vTaskDelay(TickType_t ticks) {
  ++native_storage_test_yield_count;
  native_storage_test_ticks += ticks;
}
