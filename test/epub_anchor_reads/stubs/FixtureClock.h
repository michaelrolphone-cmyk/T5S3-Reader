#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
namespace Fixture {
inline uint64_t clockMs = 0, lastYield = 0, maxYieldGap = 0;
inline unsigned delays = 0, yields = 0, locks = 0;
inline bool lockFails = false;
inline void wait() {
  maxYieldGap = std::max(maxYieldGap, clockMs - lastYield);
  lastYield = ++clockMs;
}
}
inline uint32_t millis() { return static_cast<uint32_t>(Fixture::clockMs); }
inline void delay(unsigned n) { assert(n == 1); ++Fixture::delays; Fixture::wait(); }
inline void vTaskDelay(unsigned n) { assert(n == 1); ++Fixture::yields; Fixture::wait(); }
