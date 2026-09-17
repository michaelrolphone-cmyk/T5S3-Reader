#pragma once
using TaskHandle_t = void*;
inline TaskHandle_t xTaskGetCurrentTaskHandle() {
  static int hostTask = 0;
  return &hostTask;
}
