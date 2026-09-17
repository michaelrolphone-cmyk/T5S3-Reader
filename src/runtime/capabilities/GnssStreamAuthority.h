#pragma once

#include "CapabilityAccess.h"
#include "DeviceRegistry.h"
#include <T5StreamApi.h>
#include <cstdint>
#include <cstring>

namespace RuntimeDevices {

// Firmware-only authorization bindings for already-issued GNSS record streams.
// This table is manipulated under the existing native stream mutex. It never
// stores ELF memory or an untrusted caller-supplied owner. Unlike a physical
// subscriber lease, an issued consent handle is invalidated by explicit
// release/revocation and cannot be silently replaced by a fresh grant.
class GnssStreamAuthority final {
 public:
  enum class Check : uint8_t { Unbound, Allowed, Denied };
  struct Entry {
    uint32_t owner = 0;
    DeviceHandle device = 0;
    LeaseHandle consent = 0;
    uint64_t subscription = 0;
    t5_stream_t stream = 0;
  };
  static constexpr unsigned Capacity = 4;

  GnssStreamAuthority(CapabilityAccess& access, Registry& devices)
      : access_(access), devices_(devices) {}

  bool bind(uint32_t owner, DeviceHandle device, LeaseHandle consent,
            uint64_t subscription, t5_stream_t stream) {
    if (!owner || !device || !consent || !subscription || !stream ||
        !permitted(owner, device, consent)) return false;
    Entry* free = nullptr;
    for (auto& item : entries_) {
      if (item.stream == stream || item.subscription == subscription) return false;
      if (!item.stream && !free) free = &item;
    }
    if (!free) return false;
    *free = {owner, device, consent, subscription, stream};
    return true;
  }

  Check check(uint32_t owner, t5_stream_t stream) const {
    if (!owner || !stream) return Check::Unbound;
    for (const auto& item : entries_) {
      if (item.stream != stream) continue;
      return item.owner == owner && permitted(item.owner, item.device, item.consent)
                 ? Check::Allowed : Check::Denied;
    }
    return Check::Unbound;
  }

  // Protected sources cannot enter a generic pipe: a pipe may retain an
  // already-dequeued record in an unprotected sink beyond consent revocation.
  // Introduce authorization taint propagation before permitting delegation.
  bool bound(uint32_t owner, t5_stream_t stream) const {
    for (const auto& item : entries_)
      if (item.stream == stream && item.owner == owner) return true;
    return false;
  }

  bool findStream(uint32_t owner, t5_stream_t stream, Entry* out) const {
    if (out) *out = {};
    if (!out || !owner || !stream) return false;
    for (const auto& item : entries_)
      if (item.stream == stream && item.owner == owner) {
        *out = item;
        return true;
      }
    return false;
  }
  bool forgetStream(uint32_t owner, t5_stream_t stream) {
    for (auto& item : entries_)
      if (item.stream == stream && item.owner == owner) {
        item = {};
        return true;
      }
    return false;
  }
  bool forgetSubscription(uint32_t owner, uint64_t subscription) {
    for (auto& item : entries_)
      if (item.subscription == subscription && item.owner == owner) {
        item = {};
        return true;
      }
    return false;
  }
  void releaseOwner(uint32_t owner) {
    for (auto& item : entries_) if (item.owner == owner) item = {};
  }

 private:
  bool permitted(uint32_t owner, DeviceHandle expected, LeaseHandle consent) const {
    DeviceHandle actual = 0;
    LeaseInfo lease{};
    return access_.valid(owner, consent, kCapabilityRead, &actual) &&
           actual == expected &&
           devices_.getLease(consent, owner, &lease) &&
           lease.owner == owner && lease.device == expected &&
           lease.mode == Mode::Dependency &&
           std::strcmp(lease.capability, "location.position") == 0;
  }
  CapabilityAccess& access_;
  Registry& devices_;
  Entry entries_[Capacity]{};
};

}  // namespace RuntimeDevices
