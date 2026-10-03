#pragma once
#include <cassert>
#include <mutex>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
extern thread_local bool batteryTestCritical;
inline void portENTER_CRITICAL(portMUX_TYPE* mux) {
  assert(!batteryTestCritical);
  mux->lock();
  batteryTestCritical = true;
}
inline void portEXIT_CRITICAL(portMUX_TYPE* mux) {
  assert(batteryTestCritical);
  batteryTestCritical = false;
  mux->unlock();
}
