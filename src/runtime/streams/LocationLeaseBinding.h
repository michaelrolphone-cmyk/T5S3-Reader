#pragma once

#include "CooperativeGnssProducer.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace RuntimeStreams {

// Firmware-only integration with PR #64's DeviceRegistry contract.
// Devices=RuntimeDevices::Registry, LeaseInfo=RuntimeDevices::LeaseInfo after
// branch integration. No second device registry or ELF callback is retained.
// The caller MUST authenticate the execution-context owner, use the driver
// owning task and serialize stream/coordinator calls under the stream mutex.
// Poll GPS outside that mutex; submit only a copied observation. The provider
// lease is borrowed: the driver releases it after disconnect/teardown.
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
    device_ = device; // Generation-safe physical handle, not a model identity.
    providerGrant_ = providerGrant;
    *out = token;
    return T5_STREAM_OK;
  }

  // Caller authenticates owner. Acquire a separate SHARED location.position
  // grant for each consumer, bound to the same exact device generation.
  int32_t subscribe(uint32_t owner, Token* out, t5_stream_t* stream) {
    if (out) *out = 0;
    if (stream) *stream = 0;
    if (!out || !stream || !owner) return T5_STREAM_INVALID;
    if (!reconcile()) return T5_STREAM_DISCONNECTED;
    Slot* available = nullptr;
    for (auto& slot : subscribers_) if (!slot.token) { available = &slot; break; }
    if (!available) return T5_STREAM_LIMIT;
    uint32_t grant = 0;
    // PR #64 resets out=0 on failure; default acquire mode is shared.
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

  // Provider owning task only. Never trust a caller-provided epoch: use the
  // bound device handle after verifying its actual device capability lease.
  int32_t submit(uint32_t owner, const t5_gps_state_t& copy, uint32_t sampleMs,
                 uint32_t verifiedFields = 0) {
    if (!owner || owner != providerOwner_) return T5_STREAM_DENIED;
    if (!reconcile()) return T5_STREAM_DISCONNECTED;
    return producer_.submit(subscriptions_, provider_, device_, copy, sampleMs, verifiedFields);
  }
  int32_t retry(uint32_t owner) {
    if (!owner || owner != providerOwner_) return T5_STREAM_DENIED;
    if (!reconcile()) return T5_STREAM_DISCONNECTED;
    return producer_.retry(subscriptions_, provider_, device_);
  }

  // CapabilityLost/Removed or driver shutdown. Accepted fixes remain readable
  // until DISCONNECTED. Release every subscriber device claim immediately:
  // draining old records must not prevent a replacement/exclusive provider.
  // Keep subscription tokens only for owner-checked stream drain/close.
  // The driver's borrowed provider grant is NEVER released here.
  void disconnect() {
    if (provider_) (void)subscriptions_.disconnect(provider_);
    producer_.clear();
    for (auto& slot : subscribers_) {
      if (slot.token && slot.grant) {
        (void)devices_.release(slot.grant, slot.owner);
        slot.grant = 0;
      }
    }
    provider_ = 0;
    providerOwner_ = device_ = providerGrant_ = 0;
  }
  void deviceLost(uint32_t device) {
    if (device && device == device_) disconnect();
  }

  // Capability-loss journal events and every new operation invoke reconcile().
  // A consumer which closes its stream directly must not retain its grant.
  bool reconcile() {
    if (!sourceAlive()) return false;
    reapRevoked();
    return true;
  }

  int32_t unsubscribe(uint32_t owner, Token token) {
    if (!owner || !token) return T5_STREAM_INVALID;
    for (auto& slot : subscribers_) {
      if (slot.token != token) continue;
      if (slot.owner != owner) return T5_STREAM_DENIED;
      const int32_t result = subscriptions_.unsubscribe(owner, token);
      if (slot.grant) (void)devices_.release(slot.grant, owner);
      slot = {};
      return result;
    }
    return T5_STREAM_INVALID;
  }

  // Invoke before Registry::release(owner) and driver ELF unload. Provider
  // source lease remains the responsibility of its original driver owner.
  void releaseOwner(uint32_t owner) {
    if (!owner) return;
    if (owner == providerOwner_) disconnect();
    for (auto& slot : subscribers_) {
      if (slot.token && slot.owner == owner) {
        (void)subscriptions_.unsubscribe(owner, slot.token);
        if (slot.grant) (void)devices_.release(slot.grant, owner);
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
      if (!slot.token) continue;
      // A public v2 close() may invalidate the stream without an explicit
      // semantic unsubscribe. Reclaim the corresponding device grant on the
      // next owner-task reconcile instead of retaining it until app unload.
      if (validGrant(slot.owner, device_, slot.grant) &&
          subscriptions_.streamOpen(slot.owner, slot.token)) continue;
      (void)subscriptions_.unsubscribe(slot.owner, slot.token);
      if (slot.grant) (void)devices_.release(slot.grant, slot.owner);
      slot = {};
    }
  }
};

}  // namespace RuntimeStreams
