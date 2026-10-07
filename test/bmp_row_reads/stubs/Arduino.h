#pragma once
#include <algorithm>
#include <cstdint>
inline uint64_t requestedDelays=0, clockMs=0, lastYieldClock=0, maxYieldGap=0;
inline uint32_t clockStep=0;
inline void recordYield(unsigned ms){maxYieldGap=std::max(maxYieldGap,clockMs-lastYieldClock);clockMs+=ms;lastYieldClock=clockMs;}
inline void delay(unsigned ms){requestedDelays+=ms;recordYield(ms);}
inline uint32_t millis(){const auto now=clockMs;clockMs+=clockStep;return static_cast<uint32_t>(now);}
