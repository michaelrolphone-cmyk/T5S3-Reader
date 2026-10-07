#pragma once

#include "ProviderDevicePublisher.h"
#include <cstdint>
#include <cstdio>

// Transitional compatibility adapter only: provider announcements ultimately
// need to originate in installed ELFs. Actual registry lifetime, lease
// revocation and generation identity are handled by the generic publisher.
namespace RuntimeDevices {
class UsbSerialProjection final {
 public:
  explicit UsbSerialProjection(Registry& registry) : publication_(registry) {}

  // USB event epochs and ephemeral legacy IDs form a monotonically increasing
  // binding generation. VID/PID/product are presentation, never identity.
  bool reconcile(uint32_t epoch, uint32_t legacyId, bool bound,
                 uint16_t vid, uint16_t pid, uint8_t interfaceNumber,
                 const char* product) {
    if (!bound) return clear();
    if (!legacyId || epoch == UINT32_MAX) return false;
    const uint64_t generation = (static_cast<uint64_t>(epoch) << 32u) | legacyId;
    if (generation < publication_.generation()) return false;

    char identity[kIdentityBytes]{};
    std::snprintf(identity, sizeof(identity), "usb.serial.session.%08lX",
                  static_cast<unsigned long>(legacyId));
    char label[kLabelBytes]{};
    if (product && product[0]) {
      size_t n = 0;
      while (n + 1 < sizeof(label) && product[n]) {
        const unsigned char c = static_cast<unsigned char>(product[n]);
        label[n] = c >= 0x20u && c <= 0x7eu ? static_cast<char>(c) : '?';
        ++n;
      }
      label[n] = '\0';
    } else {
      std::snprintf(label, sizeof(label), "USB Serial %04X:%04X",
                    static_cast<unsigned>(vid), static_cast<unsigned>(pid));
    }
    static constexpr const char* capabilities[] = {"serial.port", "serial.host"};
    static constexpr uint16_t versions[] = {1, 0};
    (void)interfaceNumber;
    const Descriptor descriptor{identity, label, "usb.serial", Transport::Usb,
                                capabilities, 2, 100, versions};
    if (!publication_.publish(generation, descriptor)) return false;
    legacyId_ = legacyId;
    return true;
  }

  bool clear() {
    if (!publication_.withdraw()) return false;
    legacyId_ = 0;
    return true;
  }

  DeviceHandle device() const { return publication_.device(); }
  uint32_t legacyId() const { return legacyId_; }

 private:
  ProviderDevicePublisher publication_;
  uint32_t legacyId_ = 0;
};
}  // namespace RuntimeDevices
