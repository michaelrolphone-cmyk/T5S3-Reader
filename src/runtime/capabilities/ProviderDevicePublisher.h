#pragma once

#include "DeviceRegistry.h"
#include <cstdint>
#include <cstring>

// The invocation-owner task applies copied provider announcements to the
// generic registry. No hardware handles, USB descriptors, callbacks or ELF
// function pointers are retained here. A provider must supply a monotonically
// increasing, nonzero generation for every new physical binding.
namespace RuntimeDevices {
class ProviderDevicePublisher final {
 public:
  explicit ProviderDevicePublisher(Registry& registry) : registry_(registry) {}

  // Duplicate announcements are idempotent. Changed metadata requires a NEW
  // provider generation, not a silent mutation of an already leased device.
  // Every new generation revokes the previous generation before publication.
  bool publish(uint64_t generation, const Descriptor& descriptor,
               State state = State::Available) {
    if (!generation || generation < generation_ || state == State::Removed ||
        !descriptor.capabilities || !descriptor.capabilityCount ||
        descriptor.capabilityCount > kMaxCapabilities) return false;
    if (handle_ && generation == generation_) {
      DeviceInfo current{};
      return registry_.get(handle_, &current) && same(current, descriptor) &&
             registry_.setState(handle_, state);
    }
    // Even after withdraw(), a stale token must never resurrect a lease or
    // accidentally remove a newer, still-live publication.
    if (generation <= generation_ || !withdraw()) return false;
    DeviceHandle next = 0;
    if (!registry_.add(descriptor, state, &next) || !next) return false;
    handle_ = next;
    generation_ = generation;
    return true;
  }

  // Never discard a handle on failure: an uncertain registry removal must
  // not permit duplicate publication or lose revocation obligations.
  bool withdraw() {
    if (!handle_) return true;
    if (!registry_.remove(handle_)) return false;
    handle_ = 0;
    return true;
  }

  DeviceHandle device() const { return handle_; }
  uint64_t generation() const { return generation_; }

 private:
  static bool same(const DeviceInfo& current, const Descriptor& announced) {
    if (!announced.identity || !announced.label || !announced.provider ||
        !announced.capabilities ||
        current.capabilityCount != announced.capabilityCount ||
        current.transport != announced.transport ||
        current.priority != announced.priority ||
        std::strcmp(current.identity, announced.identity) ||
        std::strcmp(current.label, announced.label) ||
        std::strcmp(current.provider, announced.provider)) return false;
    for (size_t i = 0; i < announced.capabilityCount; ++i) {
      if (!announced.capabilities[i] ||
          std::strcmp(current.capabilities[i], announced.capabilities[i]) ||
          current.capabilityApiVersions[i] !=
              (announced.capabilityApiVersions ? announced.capabilityApiVersions[i] : 0))
        return false;
    }
    return true;
  }

  Registry& registry_;
  DeviceHandle handle_ = 0;
  uint64_t generation_ = 0;
};
}  // namespace RuntimeDevices
