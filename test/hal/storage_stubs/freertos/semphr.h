#pragma once
#include <cassert>
namespace FakeLock {
inline bool held = false;
inline unsigned acquisitions = 0;
}  // namespace FakeLock
using SemaphoreHandle_t = int*;
constexpr unsigned portMAX_DELAY = ~0u;
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  static int semaphore;
  return &semaphore;
}
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) {
  assert(!FakeLock::held);
  FakeLock::held = true;
  ++FakeLock::acquisitions;
  return 1;
}
inline int xSemaphoreGive(SemaphoreHandle_t) {
  assert(FakeLock::held);
  FakeLock::held = false;
  return 1;
}
