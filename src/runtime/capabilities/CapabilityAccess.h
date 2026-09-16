#pragma once

#include "DeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimeDevices {
// This is firmware-only policy state. No grant or policy mutation function is
// exported to an ELF. A manifest dependency is NEVER an authorization grant.
// Issued handles use generation-checked registry leases in non-blocking
// Dependency mode; provider I/O must separately enforce its physical session.
constexpr uint32_t kCapabilityRead = 1u;
constexpr uint32_t kCapabilityWrite = 2u;
constexpr uint32_t kCapabilityConfigure = 4u;
constexpr uint32_t kCapabilityRightsMask = 7u;

enum class AccessResult : uint8_t {
  Ok, Invalid, Denied, Stale, Unavailable, Busy, Limit
};

class CapabilityAccess final {
 public:
  explicit CapabilityAccess(Registry& registry) : registry_(registry) {}

  // ONLY trusted firmware policy or trusted system UI may call this method.
  // No automatic grant derives from a user-editable SD manifest. Grants are
  // scoped to one invocation, exact device generation, capability and rights.
  bool grantTrusted(RuntimeResources::ExecutionContext& context,
                    DeviceHandle device, const char* capability, uint32_t rights) {
    if (!active(context) || !device || !rightsValid(rights) || !capability) return false;
    DeviceInfo info{};
    if (!registry_.get(device, &info) || !hasCapability(info, capability)) return false;
    if (owner_ && owner_ != context.id()) return false;
    for (auto& grant : grants_) {
      if (grant.used && grant.device == device &&
          std::strcmp(grant.capability, capability) == 0) {
        grant.rights |= rights;  // A new decision from trusted policy only.
        return true;
      }
    }
    Grant* free = nullptr;
    for (auto& grant : grants_) if (!grant.used && !free) free = &grant;
    if (!free) return false;
    if (!owner_) {
      if (!context.track(RuntimeResources::ExecutionContext::Resource::DeviceLeases,
                         cleanup, this)) return false;
      owner_ = context.id();
    }
    free->used = true;
    free->device = device;
    free->rights = rights;
    std::strcpy(free->capability, capability);  // Registry-validated bounded text.
    return true;
  }

  AccessResult acquire(RuntimeResources::ExecutionContext& context,
                       const char* capability, DeviceHandle device,
                       uint32_t rights, LeaseHandle* out) {
    if (out) *out = 0;
    if (!out || !device || !rightsValid(rights) || !capability) return AccessResult::Invalid;
    if (!active(context) || owner_ != context.id()) return AccessResult::Denied;
    if (!permitted(device, capability, rights)) return AccessResult::Denied;
    Issued* free = nullptr;
    for (auto& item : issued_) if (!item.used && !free) free = &item;
    if (!free) return AccessResult::Limit;
    LeaseHandle lease = 0;
    const Result result = registry_.acquire(capability, owner_, &lease, device, Mode::Dependency);
    if (result != Result::Ok) {
      switch (result) {
        case Result::Invalid: return AccessResult::Invalid;
        case Result::Busy: return AccessResult::Busy;
        case Result::Limit: return AccessResult::Limit;
        default: return AccessResult::Unavailable;
      }
    }
    free->used = true;
    free->handle = lease;
    free->device = device;
    free->rights = rights;
    std::strcpy(free->capability, capability);
    *out = lease;
    return AccessResult::Ok;
  }

  bool valid(uint32_t owner, LeaseHandle handle, uint32_t rights,
             DeviceHandle* device = nullptr) const {
    if (device) *device = 0;
    if (!owner || owner != owner_ || !handle || !rightsValid(rights)) return false;
    for (const auto& item : issued_) {
      if (!item.used || item.handle != handle || (item.rights & rights) != rights ||
          !permitted(item.device, item.capability, rights)) continue;
      LeaseInfo info{};
      if (!registry_.getLease(handle, owner, &info) || info.mode != Mode::Dependency ||
          info.device != item.device || std::strcmp(info.capability, item.capability) != 0)
        return false;
      if (device) *device = info.device;
      return true;
    }
    return false;
  }

  AccessResult release(uint32_t owner, LeaseHandle handle) {
    if (!owner || owner != owner_ || !handle) return AccessResult::Stale;
    for (auto& item : issued_) {
      if (!item.used || item.handle != handle) continue;
      (void)registry_.release(handle, owner);  // Revoked on removal is already gone.
      item = {};
      return AccessResult::Ok;
    }
    return AccessResult::Stale;
  }

  // Explicit firmware-only revocation invalidates EVERY derived access handle,
  // not merely future acquisitions. Device removal independently revokes the
  // underlying generation-qualified registry leases.
  bool revokeTrusted(uint32_t owner, DeviceHandle device, const char* capability) {
    if (!owner || owner != owner_ || !device || !capability) return false;
    for (auto& grant : grants_) {
      if (!grant.used || grant.device != device ||
          std::strcmp(grant.capability, capability) != 0) continue;
      grant = {};
      for (auto& item : issued_) {
        if (item.used && item.device == device &&
            std::strcmp(item.capability, capability) == 0) {
          (void)registry_.release(item.handle, owner);
          item = {};
        }
      }
      return true;
    }
    return false;
  }

  void releaseOwner(uint32_t owner) {
    if (!owner || owner != owner_) return;
    for (auto& item : issued_) {
      if (item.used) (void)registry_.release(item.handle, owner);
      item = {};
    }
    for (auto& grant : grants_) grant = {};
    owner_ = 0;
  }

  uint32_t owner() const { return owner_; }

 private:
  struct Grant {
    DeviceHandle device = 0;
    uint32_t rights = 0;
    char capability[kCapabilityBytes]{};
    bool used = false;
  };
  struct Issued {
    LeaseHandle handle = 0;
    DeviceHandle device = 0;
    uint32_t rights = 0;
    char capability[kCapabilityBytes]{};
    bool used = false;
  };
  static bool rightsValid(uint32_t rights) {
    return rights && !(rights & ~kCapabilityRightsMask);
  }
  static bool active(RuntimeResources::ExecutionContext& context) {
    return RuntimeResources::ExecutionContext::current() == &context &&
           context.running(context.id());
  }
  static bool hasCapability(const DeviceInfo& info, const char* capability) {
    if (!capability) return false;
    for (size_t i = 0; i < info.capabilityCount; ++i)
      if (std::strcmp(info.capabilities[i], capability) == 0) return true;
    return false;
  }
  bool permitted(DeviceHandle device, const char* capability, uint32_t rights) const {
    DeviceInfo info{};
    if (!registry_.get(device, &info) || !hasCapability(info, capability)) return false;
    for (const auto& grant : grants_)
      if (grant.used && grant.device == device &&
          std::strcmp(grant.capability, capability) == 0 &&
          (grant.rights & rights) == rights) return true;
    return false;
  }
  static void cleanup(void* opaque, uint32_t owner) {
    static_cast<CapabilityAccess*>(opaque)->releaseOwner(owner);
  }
  Registry& registry_;
  Grant grants_[8]{};
  Issued issued_[8]{};
  uint32_t owner_ = 0;
};

inline CapabilityAccess& systemCapabilityAccess() {
  static CapabilityAccess access(systemRegistry());
  return access;
}
}  // namespace RuntimeDevices
