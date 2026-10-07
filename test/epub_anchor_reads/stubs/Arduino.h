#pragma once
#include <Print.h>
#include <FixtureClock.h>
#include <cmath>
#include <cstring>
struct Esp { size_t getFreeHeap() const { return 1000000; } };
inline Esp ESP;
