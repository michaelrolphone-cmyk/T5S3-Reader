#pragma once
#include <cstdint>
#include <cstring>
class HalClock {
 public:
  void configure(const char* zone, bool storesUtc, uint8_t variant, uint32_t referenceEpoch) {
    std::strncpy(zone_, zone ? zone : "", sizeof(zone_) - 1);
    zone_[sizeof(zone_) - 1] = '\0';
    configuredStoresUtc = storesUtc;
    configuredVariant = variant;
    configuredReferenceEpoch = referenceEpoch;
    ++configureCalls;
  }
  bool syncSystemTimeFromRtc() { ++syncCalls; return true; }
  bool getRtcStoresUtc() const { return reportedStoresUtc; }
  void reset(const char* zone, bool storesUtc, uint8_t variant, uint32_t referenceEpoch) {
    configureCalls = 0; syncCalls = 0;
    std::strncpy(zone_, zone ? zone : "", sizeof(zone_) - 1);
    zone_[sizeof(zone_) - 1] = '\0';
    configuredStoresUtc = storesUtc;
    configuredVariant = variant;
    configuredReferenceEpoch = referenceEpoch;
    reportedStoresUtc = storesUtc;
  }
  char zone_[40]{};
  bool configuredStoresUtc = false;
  bool reportedStoresUtc = false;
  uint8_t configuredVariant = 0;
  uint32_t configuredReferenceEpoch = 0;
  uint32_t configureCalls = 0;
  uint32_t syncCalls = 0;
};
extern HalClock halClock;
