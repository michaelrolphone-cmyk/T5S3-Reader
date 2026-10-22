#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

#include "../../sdk/driver/RiscStorageVolumeV1.h"

// Firmware-private custody of the already-bound native source. This identifies
// a backend, not media contents or filesystem coherence. No destructor releases
// custody: the Host must retain it when selected-source cleanup is uncertain.
struct StorageSourceLease {
  const risc_storage_volume_api_v1* volume = nullptr;
  void* context = nullptr;
  uint64_t epoch = 0, token = 0;
  uint32_t owner = 0;

  bool empty() const { return !volume && !context && !epoch && !token && !owner; }
};

// HalStorage serializes identity operations with its existing storage mutex.
// Only retain() may run after that lock times out: its atomic transition cannot
// erase a simultaneous failure or taint an idle source. The exclusive Host needs
// one lease; tokens never wrap/reuse.
class StorageSourceTracker {
 public:
  explicit StorageSourceTracker(uint64_t epoch = 0, uint64_t issued = 0) : epoch_(epoch), issued_(issued) {}
  bool held() const { return lease_.token != 0; }
  // A checked-close failure during authoritative Host reads cannot be erased
  // by a later bool-only return or successful retry. Retain this source forever.
  void retain() {
    uint32_t expected = kLive;
    (void)state_.compare_exchange_strong(expected, kRetained);
  }
  bool canChange() const { return !held() && epoch_ != std::numeric_limits<uint64_t>::max(); }
  bool changed() {
    if (!canChange()) return false;
    ++epoch_;
    return true;
  }
  bool capture(const risc_storage_volume_api_v1* volume, StorageSourceLease& out) {
    if (!out.empty() || !volume || !epoch_ || held() || issued_ == std::numeric_limits<uint64_t>::max()) return false;
    lease_ = {volume, volume->context, epoch_, ++issued_, 0};
    out = lease_;
    state_.store(kLive);
    return true;
  }
  bool matches(const StorageSourceLease& lease, const risc_storage_volume_api_v1* volume) const {
    return state_.load() == kLive && held() && volume && lease.volume == volume && lease.context == volume->context &&
           lease.volume == lease_.volume && lease.context == lease_.context && lease.epoch == epoch_ &&
           lease.epoch == lease_.epoch && lease.token == lease_.token && lease.owner == lease_.owner;
  }
  bool bindOwner(StorageSourceLease& lease, uint32_t owner, const risc_storage_volume_api_v1* volume) {
    if (!owner || lease_.owner || !matches(lease, volume)) return false;
    lease_.owner = lease.owner = owner;
    return true;
  }
  bool release(StorageSourceLease& lease, const risc_storage_volume_api_v1* volume) {
    if (!matches(lease, volume)) return false;
    uint32_t expected = kLive;
    if (!state_.compare_exchange_strong(expected, kIdle)) return false;
    lease_ = {};
    lease = {};
    return true;
  }

 private:
  uint64_t epoch_ = 0, issued_ = 0;
  StorageSourceLease lease_;
  static constexpr uint32_t kIdle = 0, kLive = 1, kRetained = 2;
  std::atomic<uint32_t> state_{kIdle};
};
