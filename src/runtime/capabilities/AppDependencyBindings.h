#pragma once

#include "AppCapabilityRequirements.h"
#include <cstddef>
#include <cstdint>

namespace RuntimeDevices {

// Firmware-only dependency reservation. Mode::Dependency never authorizes I/O,
// conflicts with neither physical shared nor exclusive leases, and is revoked
// when its exact device generation disappears. A provider must still obtain its
// separate access lease and permission. No ELF pointers or callbacks are held.
class AppDependencyBindings final {
 public:
  bool bind(Registry& registry, const AppCapabilityRequirements& requirements,
            uint32_t invocation, size_t* failingIndex = nullptr) {
    if (failingIndex) *failingIndex = 0;
    if (owner_ || count_ || !invocation || requirements.count > kMaxAppRequirements)
      return false;
    for (size_t i = 0; i < requirements.count; ++i) {
      DeviceHandle device = 0;
      if (resolveRequirement(registry, requirements.entries[i], &device) != RequirementResult::Ready ||
          !device) {
        if (failingIndex) *failingIndex = i;
        rollback(registry, invocation);
        return false;
      }
      LeaseHandle lease = 0;
      if (registry.acquire(requirements.entries[i].capability, invocation, &lease,
                           device, Mode::Dependency) != Result::Ok ||
          !registry.valid(lease, invocation)) {
        if (lease) (void)registry.release(lease, invocation);
        if (failingIndex) *failingIndex = i;
        rollback(registry, invocation);
        return false;
      }
      leases_[count_++] = lease;
    }
    owner_ = invocation;
    if (failingIndex) *failingIndex = requirements.count;
    return true;
  }

  bool valid(const Registry& registry, uint32_t invocation) const {
    if (!invocation || invocation != owner_) return false;
    for (size_t i = 0; i < count_; ++i) {
      LeaseInfo info{};
      if (!registry.getLease(leases_[i], invocation, &info) ||
          info.mode != Mode::Dependency) return false;
    }
    return true;
  }

  // Releasing a revoked lease is harmless. Idempotent on repeated teardown.
  void release(Registry& registry, uint32_t invocation) {
    if (!invocation || invocation != owner_) return;
    rollback(registry, invocation);
  }

  size_t count() const { return count_; }
  uint32_t owner() const { return owner_; }

 private:
  void rollback(Registry& registry, uint32_t invocation) {
    while (count_) {
      (void)registry.release(leases_[--count_], invocation);
      leases_[count_] = 0;
    }
    owner_ = 0;
  }
  LeaseHandle leases_[kMaxAppRequirements]{};
  size_t count_ = 0;
  uint32_t owner_ = 0;
};

}  // namespace RuntimeDevices
