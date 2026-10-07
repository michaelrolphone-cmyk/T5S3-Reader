#pragma once
#include <cstdint>
#include <string>
struct String {
  std::string value;
  String() = default;
  String(std::string v) : value(std::move(v)) {}
  bool isEmpty() const { return value.empty(); }
  const char* c_str() const { return value.c_str(); }
  unsigned length() const { return value.size(); }
};
inline unsigned long millis() { return 100; }
