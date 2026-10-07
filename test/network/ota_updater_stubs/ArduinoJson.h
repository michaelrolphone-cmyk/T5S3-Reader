#pragma once
#include <cstddef>
#include <cstdint>

namespace DeserializationOption { struct Filter { template <class T> explicit Filter(const T&) {} }; }
class JsonVariantRef {
 public:
  JsonVariantRef operator[](const char*) const { return {}; }
  JsonVariantRef& operator=(bool) { return *this; }
  template <class T> T as() const { return T{}; }
  template <class T> bool is() const { return true; }
  bool operator!=(int) const { return false; }
};
class JsonObjectConst {
 public:
  JsonVariantRef operator[](const char*) const { return {}; }
};
class JsonDocument {
 public:
  template <class T> explicit JsonDocument(T*) {}
  JsonVariantRef operator[](const char*) { return {}; }
  JsonVariantRef operator[](const char*) const { return {}; }
  template <class T> bool is() const { return true; }
};
inline bool gDeserializeFails = false;
template <class T> bool deserializeJson(JsonDocument&, const char*, size_t, T) { return gDeserializeFails; }
