#pragma once
#include <FixtureClock.h>
using SemaphoreHandle_t = int*;
inline bool lockHeld = false;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int lock; return &lock; }
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) {
  ++Fixture::locks;
  assert(!lockHeld);
  if (Fixture::lockFails) return 0;
  lockHeld = true;
  return 1;
}
inline int xSemaphoreGive(SemaphoreHandle_t) { assert(lockHeld); lockHeld = false; return 1; }
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
