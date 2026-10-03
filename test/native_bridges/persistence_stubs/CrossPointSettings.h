#pragma once
#include <cstdint>
#include <cstring>
struct CrossPointSettings {
  uint8_t language = 0;
  char timeZoneId[40] = "UTC";
  uint8_t rtcStoresUtc = 0;
  uint8_t rtcVariantHint = 0;
  uint32_t rtcReferenceEpoch = 0;
  bool saveResult = true;
  mutable uint32_t saveCalls = 0;
  bool saveToFile() const { ++saveCalls; return saveResult; }
};
extern CrossPointSettings SETTINGS;
