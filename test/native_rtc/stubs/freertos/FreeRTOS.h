#pragma once
#include <cassert>
#include <mutex>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
extern thread_local bool rtcTestCritical;
inline void portENTER_CRITICAL(portMUX_TYPE* mux) {
  assert(!rtcTestCritical);
  mux->lock();
  rtcTestCritical = true;
}
inline void portEXIT_CRITICAL(portMUX_TYPE* mux) {
  assert(rtcTestCritical);
  rtcTestCritical = false;
  mux->unlock();
}
