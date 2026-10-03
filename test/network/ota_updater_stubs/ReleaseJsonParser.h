#pragma once
#include <cstddef>
#include <string>

class ReleaseJsonParser {
 public:
  inline static bool hasTag = true;
  inline static bool hasFirmware = true;
  inline static const char* version = "v9.9.9";
  inline static const char* url = "https://example.invalid/firmware.bin";
  inline static size_t size = 4096;
  explicit ReleaseJsonParser(const char*) {}
  bool foundTag() const { return hasTag; }
  bool foundFirmware() const { return hasFirmware; }
  void feed(const char*, size_t) {}
  std::string getTagName() const { return version; }
  std::string getFirmwareUrl() const { return url; }
  size_t getFirmwareSize() const { return size; }
};
