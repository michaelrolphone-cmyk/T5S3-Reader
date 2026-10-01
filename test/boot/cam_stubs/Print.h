#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
class String : public std::string {
 public:
  using std::string::string;
  bool reserve(size_t n) { std::string::reserve(n); return true; }
  void concat(const char* p, size_t n) { append(p,n); }
};
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t n) { return n; }
};
