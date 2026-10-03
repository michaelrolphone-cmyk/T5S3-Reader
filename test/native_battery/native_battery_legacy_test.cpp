#include "native/NativeBatteryGauge.h"
#include <cassert>
#include <cstdio>

// Link with no Arduino, RTOS or graph fakes: non-X4 builds must have no new
// provider dependency and leave their existing Board battery path untouched.
int main() {
  nativeBatteryTick();
  NativeBatterySnapshot sample{3800, 75, true};
  assert(!nativeBatteryReadSnapshot(&sample));
  assert(!sample.millivolts && !sample.percent && !sample.charging);
  assert(!nativeBatteryReadSnapshot(nullptr));
  std::puts("native battery legacy isolation: PASS");
}
