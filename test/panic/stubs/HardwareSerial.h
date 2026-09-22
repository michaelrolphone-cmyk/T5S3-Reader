#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include "esp_attr.h"
class Print { public: virtual void flush() {} virtual size_t write(uint8_t) { return 1; } virtual size_t write(const uint8_t*, size_t n) { return n; } };
class HWCDC { public: operator bool() const { return false; } void print(const char*) {} void begin(unsigned long) {} void end() {} };
inline HWCDC Serial;
inline unsigned long millis() { return 123; }
