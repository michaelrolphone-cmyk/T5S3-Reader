#pragma once
#include "FreeRTOS.h"
namespace FakeInventoryTime {
inline TickType_t ticks = 0;
inline unsigned yields = 0;
}
inline TickType_t xTaskGetTickCount() { return FakeInventoryTime::ticks; }
inline void vTaskDelay(TickType_t ticks) {
  FakeInventoryTime::ticks += ticks;
  ++FakeInventoryTime::yields;
}
