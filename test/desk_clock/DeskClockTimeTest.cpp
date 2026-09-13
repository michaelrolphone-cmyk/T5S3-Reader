#include "util/DeskClockTime.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>

int main() {
  using DeskClockTime::untilNextMinuteUs;
  assert(untilNextMinuteUs(0, 0) == 60000000);
  assert(untilNextMinuteUs(59, 999999) == 1);
  assert(untilNextMinuteUs(60, 0) == 60000000);
  assert(untilNextMinuteUs(86399, 500000) == 500000); // midnight
  assert(untilNextMinuteUs(86400, 250000) == 59750000);
  // Render work consumes time; the next refresh remains on the minute.
  assert(untilNextMinuteUs(12 * 3600 + 31, 123456) == 28876544);
  assert(untilNextMinuteUs(12 * 3600 + 33, 654321) == 26345679);
  // Across several days, including nonzero microseconds, the target is always
  // an exact future minute and never more than a minute away.
  for (int64_t second = -120; second < 3 * 86400; ++second) {
    for (int32_t us : {0, 123456, 999999}) {
      const auto wait = untilNextMinuteUs(second, us);
      assert(wait > 0 && wait <= 60000000);
      assert((second * 1000000 + us + static_cast<int64_t>(wait)) % 60000000 == 0);
    }
  }
}
