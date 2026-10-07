#pragma once
#include <cstddef>
class String {
 public:
  explicit String(const char* value = "") : value_(value) {}
  const char* c_str() const { return value_; }
  std::size_t length() const {
    std::size_t n = 0;
    while (value_[n] != '\0') ++n;
    return n;
  }
 private:
  const char* value_;
};
