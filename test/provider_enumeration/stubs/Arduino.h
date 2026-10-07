#pragma once
#include <Print.h>
#include <cstdint>
inline uint32_t enumerationTicks = 0;
inline unsigned enumerationYields = 0;
inline void (*enumerationYieldHook)() = nullptr;
inline uint32_t millis() { return enumerationTicks; }
inline void delay(unsigned n) {
  enumerationTicks += n;
  ++enumerationYields;
  if (enumerationYieldHook) enumerationYieldHook();
}
