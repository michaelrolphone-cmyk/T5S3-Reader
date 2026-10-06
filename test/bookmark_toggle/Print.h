#pragma once
#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t c) { return write(&c, 1); }
  virtual size_t write(const uint8_t*, size_t) = 0;
};
