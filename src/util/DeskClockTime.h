#pragma once

#include <cstdint>

namespace DeskClockTime {
// Recompute after each refresh: display latency must not accumulate into drift.
constexpr uint64_t untilNextMinuteUs(int64_t seconds, int32_t microseconds) {
  const int64_t secondInMinute = ((seconds % 60) + 60) % 60;
  return static_cast<uint64_t>((60 - secondInMinute) * 1000000LL - microseconds);
}
}  // namespace DeskClockTime
