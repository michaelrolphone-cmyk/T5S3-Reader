#pragma once

#include "PackageIdentity.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>

namespace RuntimePackages {

// The *runtime*, not a manifest or an app, owns this registry. A module must
// pin its package BEFORE dlopen and release its pin only AFTER dlclose succeeds.
// Replacement takes an exclusive reservation until all filesystem operations
// have finished, preventing a loader from racing the rename. A failed dlclose
// deliberately retains the pin. This is a lifetime gate, not a trust decision.
class PackageUseGate {
 public:
  static constexpr size_t kCapacity = 32;

  PackageUseGate() = default;
  PackageUseGate(const PackageUseGate&) = delete;
  PackageUseGate& operator=(const PackageUseGate&) = delete;

  bool pin(const char* target) {
    if (!validTarget(target)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = find(target);
    if (!slot) slot = vacant();
    if (!slot || slot->replacing || slot->pins == std::numeric_limits<uint16_t>::max()) return false;
    if (!slot->target[0]) std::strcpy(slot->target, target);
    ++slot->pins;
    return true;
  }

  bool unpin(const char* target) {
    if (!validTarget(target)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = find(target);
    if (!slot || !slot->pins) return false;
    --slot->pins;
    if (!slot->pins && !slot->replacing) slot->target[0] = '\0';
    return true;
  }

  bool beginReplacement(const char* target) {
    if (!validTarget(target)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = find(target);
    if (!slot) slot = vacant();
    if (!slot || slot->pins || slot->replacing) return false;
    if (!slot->target[0]) std::strcpy(slot->target, target);
    slot->replacing = true;
    return true;
  }

  bool endReplacement(const char* target) {
    if (!validTarget(target)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = find(target);
    if (!slot || !slot->replacing) return false;
    slot->replacing = false;
    if (!slot->pins) slot->target[0] = '\0';
    return true;
  }

  bool pinned(const char* target) {
    if (!validTarget(target)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const Slot* slot = find(target);
    return slot && slot->pins != 0;
  }

 private:
  struct Slot {
    char target[96]{};
    uint16_t pins = 0;
    bool replacing = false;
  };
  std::mutex mutex_;
  Slot slots_[kCapacity]{};

  static bool validTarget(const char* target) {
    if (!target) return false;
    // Only validated package-directory roots, never arbitrary /sd paths.
    constexpr const char* roots[] = {"/Drivers/", "/Services/", "/Providers/", "/Apps/"};
    for (const char* root : roots) {
      const size_t length = std::strlen(root);
      if (std::strncmp(target, root, length) != 0) continue;
      return safeId(target + length) && std::strlen(target) < sizeof(Slot::target);
    }
    return false;
  }

  Slot* find(const char* target) {
    for (auto& slot : slots_)
      if (slot.target[0] && std::strcmp(slot.target, target) == 0) return &slot;
    return nullptr;
  }
  Slot* vacant() {
    for (auto& slot : slots_) if (!slot.target[0]) return &slot;
    return nullptr;
  }
};

// An inline function's local static denotes one process-wide instance across
// translation units. All installed ELF loaders must register with this gate.
inline PackageUseGate& systemPackageUseGate() {
  static PackageUseGate gate;
  return gate;
}

class PackageReplacementLease {
 public:
  explicit PackageReplacementLease(const char* target)
      : target_(target), acquired_(systemPackageUseGate().beginReplacement(target)) {}
  ~PackageReplacementLease() {
    if (acquired_) (void)systemPackageUseGate().endReplacement(target_);
  }
  PackageReplacementLease(const PackageReplacementLease&) = delete;
  PackageReplacementLease& operator=(const PackageReplacementLease&) = delete;
  explicit operator bool() const { return acquired_; }
 private:
  const char* target_;
  bool acquired_;
};

}  // namespace RuntimePackages
