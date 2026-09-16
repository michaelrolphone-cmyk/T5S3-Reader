#include "runtime/streams/LocationLeaseBinding.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
using namespace RuntimeStreams;
namespace {
// PR #64-compatible acquire/getLease/release test double. Do not vendor a
// competing DeviceRegistry while the real parent implementation is in review.
struct LeaseInfo { uint32_t handle = 0, device = 0, owner = 0; char capability[40]{}; };
class Devices {
 public:
  enum class Result { Ok, Denied };
  void setDevice(uint32_t handle) { device_ = handle; available_ = true; }
  void lose() { available_ = false; for (auto& s : leases_) s.live = false; }
  void revoke(uint32_t handle) {
    for (auto& s : leases_) if (s.live && s.info.handle == handle) s.live = false;
  }
  Result acquire(const char* capability, uint32_t owner, uint32_t* out, uint32_t device) {
    if (out) *out = 0;
    if (!out || !owner || !available_ || device != device_ ||
        std::strcmp(capability, "location.position")) return Result::Denied;
    for (auto& s : leases_) if (!s.live) {
      s.live = true;
      s.info = {};
      s.info.handle = ++generation_;
      s.info.device = device;
      s.info.owner = owner;
      std::strcpy(s.info.capability, capability);
      *out = s.info.handle;
      return Result::Ok;
    }
    return Result::Denied;
  }
  bool getLease(uint32_t lease, uint32_t owner, LeaseInfo* out) const {
    if (!available_ || !out || !owner) return false;
    for (const auto& s : leases_)
      if (s.live && s.info.handle == lease && s.info.owner == owner) {
        *out = s.info;
        return true;
      }
    return false;
  }
  Result release(uint32_t lease, uint32_t owner) {
    for (auto& s : leases_) if (s.live && s.info.handle == lease && s.info.owner == owner) {
      s.live = false;
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
    for (const auto& s : leases_) if (s.live) ++count;
    return count;
  }
 private:
  struct Slot { LeaseInfo info{}; bool live = false; };
  Slot leases_[8]{};
  uint32_t device_ = 0, generation_ = 100;
  bool available_ = false;
};
t5_gps_state_t fix() {
  t5_gps_state_t f{};
  f.status = T5_GPS_STATUS_FIX;
  f.fix_valid = f.receiver_detected = 1;
  f.latitude = 44.532385;
  f.longitude = -116.056066;
  f.age_ms = 12;
  return f;
}
void readOne(Registry& r, uint32_t owner, t5_stream_t stream, uint32_t expected) {
  uint8_t bytes[GnssRecordAdapter::Size]{};
  uint32_t n = 0;
  assert(r.readRecord(owner, stream, bytes, sizeof(bytes), &n) == T5_STREAM_OK && n == sizeof(bytes));
  const unsigned sample = unsigned(bytes[RISCRTE_FIX_OFFSET_SAMPLE_MS]) |
                          (unsigned(bytes[RISCRTE_FIX_OFFSET_SAMPLE_MS + 1]) << 8);
  assert(sample == expected);
}
}
int main() {
  Devices devices;
  Registry streams;
  LocationPositionSubscriptions subscriptions(streams);
  LocationLeaseBinding<Devices, LeaseInfo> binding(devices, subscriptions);
  devices.setDevice(257);
  const uint32_t providerGrant = devices.grant(10);
  LocationPositionSubscriptions::Lease provider = 99, a = 0, b = 0;
  t5_stream_t sa = 0, sb = 0;
  assert(binding.attach(11, 257, providerGrant, &provider) == T5_STREAM_DENIED && !provider);
  assert(binding.attach(10, 258, providerGrant, &provider) == T5_STREAM_DENIED && !provider);
  assert(binding.attach(10, 257, providerGrant, &provider) == T5_STREAM_OK && provider);
  assert(binding.attach(10, 257, providerGrant, &a) == T5_STREAM_BUSY && !a);
  assert(binding.subscribe(21, &a, &sa) == T5_STREAM_OK && a && sa);
  assert(binding.subscribe(22, &b, &sb) == T5_STREAM_OK && b && sb);
  assert(devices.count() == 3);
  assert(streams.writeRecord(21, sa, "spoof", 5) == T5_STREAM_DENIED);

  // The public stream API can close a record handle without semantic
  // unsubscribe. A subsequent reconcile must reclaim its independent device
  // lease and slot; neither the driver nor other subscribers may leak claims.
  LocationPositionSubscriptions::Lease closed = 0;
  t5_stream_t closedStream = 0;
  assert(binding.subscribe(23, &closed, &closedStream) == T5_STREAM_OK);
  assert(devices.count() == 4 && subscriptions.streamOpen(23, closed));
  assert(streams.close(23, closedStream) == T5_STREAM_OK);
  assert(!subscriptions.streamOpen(23, closed));
  assert(binding.reconcile() && devices.count() == 3);
  assert(subscriptions.subscribers() == 2);
  assert(binding.unsubscribe(23, closed) == T5_STREAM_INVALID);

  auto f = fix();
  for (uint32_t i = 0; i < 4; ++i)
    assert(binding.submit(10, f, 1000 + i) == T5_STREAM_OK);
  assert(binding.submit(11, f, 1004) == T5_STREAM_DENIED);
  assert(binding.submit(10, f, 1004) == T5_STREAM_AGAIN && binding.hasPending());
  LeaseInfo old{};
  bool found = false;
  for (uint32_t grant = 101; grant < 110; ++grant)
    if (devices.getLease(grant, 22, &old)) { devices.revoke(grant); found = true; break; }
  assert(found);
  assert(binding.retry(10) == T5_STREAM_AGAIN && binding.hasPending());
  uint32_t count = 0;
  assert(streams.readRecord(22, sb, nullptr, 0, &count) == T5_STREAM_INVALID);
  readOne(streams, 21, sa, 1000);
  assert(binding.retry(10) == T5_STREAM_OK && !binding.hasPending());
  assert(subscriptions.subscribers() == 1);
  for (uint32_t i = 1; i <= 4; ++i) readOne(streams, 21, sa, 1000 + i);
  assert(binding.unsubscribe(22, b) == T5_STREAM_INVALID);
  assert(binding.submit(10, f, 1100) == T5_STREAM_OK);
  devices.lose();
  assert(!binding.reconcile() && !binding.hasPending() && !binding.provider());
  readOne(streams, 21, sa, 1100);
  uint8_t bytes[GnssRecordAdapter::Size]{};
  assert(streams.readRecord(21, sa, bytes, sizeof(bytes), &count) == T5_STREAM_DISCONNECTED);
  assert(binding.subscribe(23, &b, &sb) == T5_STREAM_DISCONNECTED && !b && !sb);
  assert(binding.unsubscribe(21, a) == T5_STREAM_OK);
  assert(devices.count() == 0);
  assert(binding.attach(10, 257, providerGrant, &provider) == T5_STREAM_DENIED);
  devices.setDevice(513);
  const uint32_t nextGrant = devices.grant(12);
  assert(binding.attach(12, 257, nextGrant, &provider) == T5_STREAM_DENIED);
  assert(binding.attach(12, 513, nextGrant, &provider) == T5_STREAM_OK);
  assert(binding.subscribe(24, &a, &sa) == T5_STREAM_OK);
  assert(binding.submit(12, f, 1200) == T5_STREAM_OK);
  binding.deviceLost(257);
  assert(binding.provider());
  binding.releaseOwner(24);
  assert(subscriptions.subscribers() == 0);
  assert(devices.count() == 1);

  // Voluntary driver shutdown differs from physical loss: registry leases
  // remain live unless explicitly released, but old read-only data MUST drain.
  LocationPositionSubscriptions::Lease draining = 0;
  t5_stream_t drainStream = 0;
  assert(binding.subscribe(25, &draining, &drainStream) == T5_STREAM_OK);
  for (uint32_t i = 0; i < 4; ++i)
    assert(binding.submit(12, f, 1300 + i) == T5_STREAM_OK);
  assert(binding.submit(12, f, 1304) == T5_STREAM_AGAIN && binding.hasPending());
  assert(devices.count() == 2);
  binding.disconnect();
  assert(!binding.provider() && !binding.hasPending());
  assert(devices.count() == 1); // Driver's borrowed grant only.
  assert(binding.attach(12, 513, nextGrant, &provider) == T5_STREAM_BUSY);
  for (uint32_t i = 0; i < 4; ++i) readOne(streams, 25, drainStream, 1300 + i);
  assert(streams.readRecord(25, drainStream, bytes, sizeof(bytes), &count) == T5_STREAM_DISCONNECTED);
  assert(binding.unsubscribe(25, draining) == T5_STREAM_OK && subscriptions.subscribers() == 0);
  assert(devices.count() == 1);
  assert(binding.attach(12, 513, nextGrant, &provider) == T5_STREAM_OK);
  binding.releaseOwner(12);
  assert(!binding.provider() && devices.count() == 1);
  assert(devices.release(nextGrant, 12) == Devices::Result::Ok);
  streams.release(10); streams.release(12); streams.release(21); streams.release(22); streams.release(23);
  streams.release(24); streams.release(25);
  std::cout << "Location lease binding authorization, drain and revocation tests passed\n";
}
