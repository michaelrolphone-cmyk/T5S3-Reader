#include "PackageVerificationReceipt.h"
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <esp_random.h>
#endif
namespace RuntimePackages {
bool newPackageReceiptGeneration(uint8_t (&out)[16]) {
  std::memset(out, 0, sizeof(out));
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    esp_fill_random(out, sizeof(out));
    if (receiptAny(out, sizeof(out))) return true;
  }
#endif
  return false;
}
}  // namespace RuntimePackages
