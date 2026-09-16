#include "runtime/streams/LocationLeaseBinding.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace RuntimeStreams;

namespace {
// Implements the same minimal acquire/getLease/release contract as PR #64's
// single-task RuntimeDevices::Registry. The PR #64 header is intentionally not
// copied into this independent branch; cross-branch CI is still required.
struct LeaseInfo {
  uint32_t handle = 0, device = 0, owner = 0;
  char capability[40]{};
};
class Devices {
 public:
  enum class Result { Ok, Denied };
  void setDevice(uint32_t handle) { device_ = handle; available_ = true; }
  void lose() { available_ = false; for (auto& slot : leases_) slot.live = false; }
  void revoke(uint32_t handle) {
    for (auto& slot : leases_) if (slot.live && slot.info.handle == handle) slot.live = false;
  }
  Result acquire(const char* capability, uint32_t owner, uint32_t* out, uint32_t device) {
    if (out) *out = 0;
    if (!out || !owner || !available_ || device != device_ || std::strcmp(capability, "location.position"))
      return Result::Denied;
    for (auto& slot : leases_) if (!slot.live) {
      slot.live = true;
      slot.info = {};
      slot.info.handle = ++generation_;
      slot.info.owner = owner;
      slot.info.device = device;
      std::strcpy(slot.info.capability, capability);
      *out = slot.info.handle;
      return Result::Ok;
    }
    return Result::Denied;
  }
  bool getLease(uint32_t lease, uint32_t owner, LeaseInfo* out) const {
    if (!available_ || !out || !owner) return false;
    for (const auto& slot : leases_)
      if (slot.live && slot.info.handle == lease && slot.info.owner == owner) {
        *out = slot.info;
        return true;
      }
    return false;
  }
  Result release(uint32_t lease, uint32_t owner) {
    for (auto& slot : leases_) if (slot.live && slot.info.handle == lease && slot.info.owner == owner) {
      slot.live = false;
      return Result::Ok;
    }
    return Result::Denied;
  }
  uint32_t grant(uint32_t owner) {
    uint32_t value = 0;
    assert(acquire("location.position", owner, &value, device_) == Result::Ok && value);
    return value;
  }
  size_t count() const {
    size_t count = 0;
    for (const auto& slot : leases_) if (slot.live) ++count;
    return count;
  }
 private:
  struct Slot { LeaseInfo info{}; bool live = false; };
  Slot leases_[8]{};
  uint32_t device_ = 0, generation_ = 100;
  bool available_ = false;
};
t5_gps_state_t fix() {
  t5_gps_state_t result{};
  result.status = T5_GPS_STATUS_FIX;
  result.fix_valid = result.receiver_detected = 1;
  result.latitude = 44.532385;
  result.longitude = -116.056066;
  result.age_ms = 12;
  return result;
}
void readOne(Registry& registry, uint32_t owner, t5_stream_t stream, uint32_t expectedTime) {
  uint8_t bytes[GnssRecordAdapter::Size]{};
  uint32_t n = 0;
  assert(registry.readRecord(owner, stream, bytes, sizeof(bytes), &n) == T5_STREAM_OK && n == sizeof(bytes));
  const unsigned sample = unsigned(bytes[RISCRTE_FIX_OFFSET_SAMPLE_MS]) |
                          (unsigned(bytes[RISCRTE_FIX_OFFSET_SAMPLE_MS + 1]) << 8);
  assert(sample == expectedTime);
}
}

int main() {
  Devices devices;
  Registry streams;
  LocationPositionSubscriptions subscriptions(streams);
  LocationLeaseBinding<Devices, LeaseInfo> bound(devices, subscriptions);
  devices.setDevice(257);  // PR #64's generation-aware physical device handle.
  uint32_t providerGrant = devices.grant(10);
  LocationPositionSubscriptions::Lease provider = 99, a = 0, b = 0;
  t5_stream_t sa = 0, sb = 0;
  assert(bound.attach(11, 257, providerGrant, &provider) == T5_STREAM_DENIED && !provider);
  assert(bound.attach(10, 258, providerGrant, &provider) == T5_STREAM_DENIED && !provider);
  assert(bound.attach(10, 257, providerGrant, &provider) == T5_STREAM_OK && provider);
  assert(bound.attach(10, 257, providerGrant, &a) == T5_STREAM_BUSY && !a);
  assert(bound.subscribe(21, &a, &sa) == T5_STREAM_OK && a && sa);
  assert(bound.subscribe(22, &b, &sb) == T5_STREAM_OK && b && sb);
  assert(devices.count() == 3); // One borrowed provider plus two subscriber grants.
  assert(streams.writeRecord(21, sa, "spoof", 5) == T5_STREAM_DENIED);
  auto observation = fix();
  for (uint32_t i = 0; i < 4; ++i)
    assert(bound.submit(10, observation, 1000 + i) == T5_STREAM_OK);
  assert(bound.submit(11, observation, 1004) == T5_STREAM_DENIED);
  assert(bound.submit(10, observation, 1004) == T5_STREAM_AGAIN && bound.hasPending());
  // Revoking one consumer MUST close its stream and release its grant before
  // attempting an all-or-nothing publish to the remaining live consumer.
  LeaseInfo revoked{};
  bool found = false;
  for (uint32_t lease = 101; lease < 110; ++lease)
    if (devices.getLease(lease, 22, &revoked)) { devices.revoke(lease); found = true; break; }
  assert(found);
  assert(bound.retry(10) == T5_STREAM_AGAIN && bound.hasPending());
  assert(streams.readRecord(22, sb, nullptr, 0, &providerGrant) == T5_STREAM_INVALID);
  readOne(streams, 21, sa, 1000);
  assert(bound.retry(10) == T5_STREAM_OK && !bound.hasPending());
  assert(subscriptions.subscribers() == 1);
  for (uint32_t i = 1; i <= 4; ++i) readOne(streams, 21, sa, 1000 + i);
  assert(bound.unsubscribe(22, b) == T5_STREAM_INVALID); // Revoked token cannot close a replacement.

  // A valid capability is required for each new subscriber; source capability
  // loss must finish even a queue containing accepted, unread observations.
  assert(bound.submit(10, observation, 1100) == T5_STREAM_OK);
  devices.lose();
  assert(!bound.reconcile() && !bound.hasPending() && !bound.provider());
  readOne(streams, 21, sa, 1100);
  uint8_t bytes[GnssRecordAdapter::Size]{};
  uint32_t size = 0;
  assert(streams.readRecord(21, sa, bytes, sizeof(bytes), &size) == T5_STREAM_DISCONNECTED);
  assert(bound.subscribe(23, &b, &sb) == T5_STREAM_DISCONNECTED && !b && !sb);
  assert(bound.unsubscribe(21, a) == T5_STREAM_OK);
  assert(devices.count() == 0);
  assert(bound.attach(10, 257, providerGrant, &provider) == T5_STREAM_DENIED);
  devices.setDevice(513); // Identical hardware replug, new generation.
  const uint32_t nextGrant = devices.grant(12);
  assert(bound.attach(12, 257, nextGrant, &provider) == T5_STREAM_DENIED);
  assert(bound.attach(12, 513, nextGrant, &provider) == T5_STREAM_OK);
  assert(bound.subscribe(24, &a, &sa) == T5_STREAM_OK);
  assert(bound.submit(12, observation, 1200) == T5_STREAM_OK);
  bound.deviceLost(257); // Stale loss event cannot disconnect replacement.
  assert(bound.provider());
  bound.releaseOwner(24);
  assert(subscriptions.subscribers() == 0);
  assert(devices.count() == 1); // The driver's borrowed provider lease remains.
  bound.releaseOwner(12);
  assert(!bound.provider());
  assert(devices.count() == 1);
  assert(devices.release(nextGrant, 12) == Devices::Result::Ok);
  streams.release(10); streams.release(12); streams.release(21); streams.release(22); streams.release(24);
  std::cout << "Location lease binding authorization and revocation tests passed\n";
}
