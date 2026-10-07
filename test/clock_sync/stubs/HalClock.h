#pragma once
#include <cstdint>
class HalClock {
 public:
  bool isSystemTimeValid() const;
  bool syncRtcFromSystemTime();
  uint8_t getVariantHint() const;
  void configure(const char* timezone, bool storesUtc, uint8_t variantHint, uint32_t referenceEpoch);
};
extern HalClock halClock;
unsigned long millis();
void vTaskDelay(uint32_t ticks);
