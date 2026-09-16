#pragma once

#include "CooperativeGnssProducer.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace RuntimeStreams {

// The only authority used here is the Unified Device Registry's EXISTING
// capability lease. Devices matches RuntimeDevices::Registry; LeaseInfo matches
// RuntimeDevices::LeaseInfo in PR #64. This template deliberately does not
// introduce a second device registry or compile-depend on that parallel PR.
//
// Firmware callers MUST authenticate the current execution-context owner and
// run on the device/driver owning task, under the stream registry's mutex.
// DeviceRegistry is single-task-owned; no cross-task device calls are allowed.
// Poll the installable GPS driver OUTSIDE the stream mutex, then submit a copy.
// A provider lease is borrowed from the driver and NEVER released here; the
// driver is responsible for releasing it after this binding is disconnected.
template <typename Devices, typename LeaseInfo>
class LocationLeaseBinding final {
 public:
  using Token = LocationPositionSubscriptions::Lease;
  static constexpr unsigned MaxSubscribers = LocationPositionSubscriptions::MaxSubscribers;

  LocationLeaseBinding(Devices& devices, LocationPositionSubscriptions& subscriptions)
      : devices_(devices), subscriptions_(subscriptions) {}
  LocationLeaseBinding(const LocationLeaseBinding&) = delete;
  LocationLeaseBinding& operator=(const LocationLeaseBinding&) = delete;

  int32_t attach(uint32_t owner, uint32_t device, uint32_t providerGrant, Token* out) {
    if (out) *out = 0;
    if (!out || !owner || !device || !providerGrant) return T5_STREAM_INVALID;
    if (provider_) return T5_STREAM_BUSY;
    if (!validGrant(owner, device, providerGrant)) return T5_STREAM_DENIED;
    Token token = 0;
    const int32_t result = subscriptions_.attachProvider(owner, device, &token);
    if (result != T5_STREAM_OK) return result;
    provider_ = token;
    providerOwner_ = owner;
    device_ = device;  // Generation-safe physical handle, never VID/PID identity.
    providerGrant_ = providerGrant;
    *out = token;
    return T5_STREAM_OK;
  }

  // Caller must authenticate owner before entering this firmware-only API.
  // The resolver grants one independently owned, SHARED location.position
  // capability lease per subscriber. A successful stream is READ-only.
  int32_t subscribe(uint32_t owner, Token* out, t5_stream_t* stream) {
    if (out) *out = 0;
    if (stream) *stream = 0;
    if (!out || !stream || !owner) return T5_STREAM_INVALID;
    if (!sourceAlive()) return T5_STREAM_DISCONNECTED;
    reapRevoked();
    Slot* available = nullptr;
    for (auto& slot : subscribers_) if (!slot.token) { available = &slot; break; }
    if (!available) return T5_STREAM_LIMIT;
    uint32_t grant = 0;
    // PR #64's acquire() resets grant on failure; zero never denotes a lease.
    (void)devices_.acquire("location.position", owner, &grant, device_);
    if (!grant) return T5_STREAM_DENIED;
    if (!validGrant(owner, device_, grant)) {
      (void)devices_.release(grant, owner);
      return T5_STREAM_DENIED;
    }
    Token token = 0;
    t5_stream_t endpoint = 0;
    const int32_t result = subscriptions_.subscribe(provider_, owner, &token, &endpoint);
    if (result != T5_STREAM_OK) {
      (void)devices_.release(grant, owner);
      return result;
    }
    *available = {owner, grant, token};
    *out = token;
    *stream = endpoint;
    return T5_STREAM_OK;
  }

  // Invoke only from the provider's claiming task AFTER a successful driver
  // read; never retain driver callbacks and never publish a caller-provided
  // epoch. The bound, generation-safe device handle is authoritative.
  int32_t submit(uint32_t owner, const t5_gps_state_t& copy, uint32_t sampleMs,
                 uint32_t verifiedFields = 0) {
    if (!owner || owner != providerOwner_) return T5_STREAM_DENIED;
    if (!sourceAlive()) return T5_STREAM_DISCONNECTED;
    reapRevoked();
    return producer_.submit(subscriptions_, provider_, device_, copy, sampleMs, verifiedFields);
  }

  // When a record is pending, the owner MUST retry it before polling hardware
  // again. A revoked provider/device clears pending and finishes subscriptions.
  int32_t retry(uint32_t owner) {
    if (!owner || owner != providerOwner_) return T5_STREAM_DENIED;
    if (!sourceAlive()) return T5_STREAM_DISCONNECTED;
    reapRevoked();
    return producer_.retry(subscriptions_, provider_, device_);
  }

  // Called on capability-lost/removed events or before driver unload.
  // Existing accepted records drain to DISCONNECTED. The physical lease is
  // borrowed and must be released by the GPS driver itself.
  void disconnect() {
    if (provider_) (void)subscriptions_.disconnect(provider_);
    producer_.clear();
    provider_ = 0;
    providerOwner_ = device_ = providerGrant_ = 0;
  }
  void deviceLost(uint32_t device) {
    if (device && device == device_) disconnect();
  }
  bool reconcile() { return sourceAlive(); }

  int32_t unsubscribe(uint32_t owner, Token token) {
    if (!owner || !token) return T5_STREAM_INVALID;
    for (auto& slot : subscribers_) {
      if (slot.token != token) continue;
      if (slot.owner != owner) return T5_STREAM_DENIED;
      const int32_t result = subscriptions_.unsubscribe(owner, token);
      (void)devices_.release(slot.grant, owner);
      slot = {};
      return result;
    }
    return T5_STREAM_INVALID;
  }

  // Invoke before Registry::release(owner) and before the driver's unload.
  // App teardown cannot leave a subscriber device grant behind.
  void releaseOwner(uint32_t owner) {
    if (!owner) return;
    if (owner == providerOwner_) disconnect();
    for (auto& slot : subscribers_) {
      if (slot.token && slot.owner == owner) {
        (void)subscriptions_.unsubscribe(owner, slot.token);
        (void)devices_.release(slot.grant, owner);
        slot = {};
      }
    }
    subscriptions_.releaseOwner(owner);
  }

  bool hasPending() const { return producer_.hasPending(); }
  uint32_t device() const { return device_; }
  Token provider() const { return provider_; }

 private:
  struct Slot { uint32_t owner = 0, grant = 0; Token token = 0; };
  Devices& devices_;
  LocationPositionSubscriptions& subscriptions_;
  CooperativeGnssProducer producer_{};
  std::array<Slot, MaxSubscribers> subscribers_{};
  Token provider_ = 0;
  uint32_t providerOwner_ = 0, device_ = 0, providerGrant_ = 0;

  bool validGrant(uint32_t owner, uint32_t device, uint32_t grant) const {
    LeaseInfo info{};
    return owner && device && grant && devices_.getLease(grant, owner, &info) &&
           info.owner == owner && info.device == device &&
           std::strcmp(info.capability, "location.position") == 0;
  }
  bool sourceAlive() {
    if (!provider_) return false;
    if (validGrant(providerOwner_, device_, providerGrant_)) return true;
    disconnect();
    return false;
  }
  void reapRevoked() {
    for (auto& slot : subscribers_) {
      if (!slot.token || validGrant(slot.owner, device_, slot.grant)) continue;
      (void)subscriptions_.unsubscribe(slot.owner, slot.token);
      (void)devices_.release(slot.grant, slot.owner);
      slot = {};
    }
  }
};

}  // namespace RuntimeStreams
