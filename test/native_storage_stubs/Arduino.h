#pragma once

#include <cstddef>
#include <string>

class String {
public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  size_t length() const { return value_.size(); }
  const char* c_str() const { return value_.c_str(); }
  bool reserve(size_t size) { value_.reserve(size); return true; }
  String& operator+=(char value) { value_.push_back(value); return *this; }

private:
  std::string value_;
};
