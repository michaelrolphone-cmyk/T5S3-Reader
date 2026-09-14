#pragma once
#include <cstdint>
#include <string>
#include <vector>
extern std::vector<std::string> calls;
inline void delay(unsigned ms) { calls.push_back("delay:" + std::to_string(ms)); }
class String {
  std::string value;
 public:
  String(const char* text) : value(text) {}
  const char* c_str() const { return value.c_str(); }
  void replace(const char* from, const char* to) {
    size_t pos;
    while ((pos = value.find(from)) != std::string::npos) value.replace(pos, std::string(from).size(), to);
  }
  friend String operator+(const char* lhs, const String& rhs) { return String((lhs + rhs.value).c_str()); }
};
