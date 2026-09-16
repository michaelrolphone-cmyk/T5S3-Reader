#pragma once

#include "DeviceRegistry.h"
#include <cstdint>
#include <cstdio>

// Firmware-only adapter. The USB host publishes a lock-protected snapshot;
// the native app owner task calls reconcile() with that snapshot. Never call
// this class from a USB callback or make the unified Registry cross-core.
namespace RuntimeDevices {
class UsbSerialProjection final {
 public:
  explicit UsbSerialProjection(Registry& registry) : registry_(registry) {}

  // A legacy USB ID identifies an enumeration session, NOT the underlying
  // permanent device. Do not claim persistent identity from VID/PID/product.
  // epoch changes on disconnect, even if an identical peripheral reattaches.
  bool reconcile(uint32_t epoch, uint32_t legacyId, bool bound,
                 uint16_t vid, uint16_t pid, uint8_t interfaceNumber,
                 const char* product) {
    if (bound && (!legacyId || epoch == UINT32_MAX)) return clear();
    if (epoch == epoch_ && legacyId == legacyId_ && bound == (handle_ != 0)) {
      DeviceInfo info{};
      if (!handle_ || registry_.get(handle_, &info)) return true;
    }
    clear();
    epoch_ = epoch;
    if (!bound) return true;

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
    // serial.port exposes the versioned serial-port v1 ABI; serial.host has no
    // public semantic contract yet. Keep its version UNKNOWN, never guessed.
    static constexpr uint16_t capabilityVersions[] = {1, 0};
    (void)interfaceNumber;
    const Descriptor descriptor{identity, label, "usb.serial", Transport::Usb,
                                capabilities, 2, 100, capabilityVersions};
    if (!registry_.add(descriptor, State::Available, &handle_)) {
      handle_ = 0;
      return false;
    }
    legacyId_ = legacyId;
    return true;
  }

  bool clear() {
    if (handle_) {
      (void)registry_.remove(handle_);
      handle_ = 0;
    }
    legacyId_ = 0;
    epoch_ = UINT32_MAX;
    return true;
  }

  DeviceHandle device() const { return handle_; }
  uint32_t legacyId() const { return legacyId_; }

 private:
  Registry& registry_;
  DeviceHandle handle_ = 0;
  uint32_t legacyId_ = 0;
  uint32_t epoch_ = UINT32_MAX;
};
}  // namespace RuntimeDevices