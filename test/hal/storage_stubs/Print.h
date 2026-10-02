#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
using std::min;
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& value) : std::string(value) {}
  bool endsWith(const char* tail) const {
    const std::string suffix(tail);
    return size() >= suffix.size() && compare(size() - suffix.size(), suffix.size(), suffix) == 0;
  }
};
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* bytes, size_t size) {
    for (size_t i = 0; i < size; ++i)
      if (!write(bytes[i])) return i;
    return size;
  }
};
