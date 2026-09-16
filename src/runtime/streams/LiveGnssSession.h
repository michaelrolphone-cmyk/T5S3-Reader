#pragma once

#include "LocationLeaseBinding.h"
#include "runtime/capabilities/DeviceRegistry.h"
#include <cstdint>

namespace RuntimeStreams {

// Firmware-only, owner-task control plane for the live GNSS data path. Exactly
// one shared stream Registry must be injected: NEVER construct a second stream
// registry here. The native bridge holds its existing stream mutex around
// every method; it releases that mutex for the actual UART/driver read and
// calls publishCopied() with its stack-local value afterwards.
//
// A provider grant is BORROWED from the installable GPS driver; only its owner
// may release it. App-facing callers must be authenticated and authorized by
// firmware before invoking subscribe(). No ELF pointers/callbacks are stored.
class LiveGnssSession final {
 public:
  using Token = LocationPositionSubscriptions::Lease;
  enum class PollDecision : uint8_t {
    Poll,             // No pending record; safe to leave mutex and read UART.
    Retried,          // Previously blocked record was delivered; skip this poll.
    Backpressured,    // Record still blocked; do NOT read newer GPS data.
    Disconnected,     // Provider lease/device has gone away.
    Denied            // Wrong execution-context owner.
  };

  LiveGnssSession(RuntimeDevices::Registry& devices, Registry& streams)
      : subscriptions_(streams), binding_(devices, subscriptions_) {}
  LiveGnssSession(const LiveGnssSession&) = delete;
  LiveGnssSession& operator=(const LiveGnssSession&) = delete;

  int32_t attach(uint32_t owner, uint32_t device, uint32_t borrowedGrant) {
    if (!owner || !device || !borrowedGrant) return T5_STREAM_INVALID;
    Token token = 0;
    const int32_t result = binding_.attach(owner, device, borrowedGrant, &token);
    if (result == T5_STREAM_OK) owner_ = owner;
    return result;
  }

  // Called by the same claiming task BEFORE a driver read. A blocked record is
  // retried once and consumes this tick even if retry succeeds. This prevents
  // a fresh observation from overwriting an undelivered sample.
  PollDecision beforePoll(uint32_t owner) {
    if (!owner || owner != owner_) return PollDecision::Denied;
    if (!binding_.reconcile()) return PollDecision::Disconnected;
    if (!binding_.hasPending()) return PollDecision::Poll;
    const int32_t result = binding_.retry(owner);
    if (result == T5_STREAM_OK) return PollDecision::Retried;
    if (result == T5_STREAM_AGAIN && binding_.hasPending()) return PollDecision::Backpressured;
    if (result == T5_STREAM_DISCONNECTED) return PollDecision::Disconnected;
    return PollDecision::Retried; // Terminal error cleared pending; next tick can poll.
  }

  // No provider reads, callbacks, or ELF pointers under the bridge mutex.
  int32_t publishCopied(uint32_t owner, const t5_gps_state_t& observation,
                        uint32_t sampleMs, uint32_t verifiedFields = 0) {
    if (!owner || owner != owner_) return T5_STREAM_DENIED;
    if (binding_.hasPending()) return T5_STREAM_BUSY;
    return binding_.submit(owner, observation, sampleMs, verifiedFields);
  }

  // Firmware must validate the CURRENT caller's identity plus an explicit
  // READ authorization for this device/capability before invoking subscribe.
  int32_t subscribe(uint32_t authenticatedOwner, Token* token, t5_stream_t* stream) {
    return binding_.subscribe(authenticatedOwner, token, stream);
  }
  int32_t unsubscribe(uint32_t authenticatedOwner, Token token) {
    return binding_.unsubscribe(authenticatedOwner, token);
  }

  // Invoke before driver ELF unload and before releasing its source grant.
  // Accepted records are finished for drain, not silently discarded.
  void disconnect() { binding_.disconnect(); owner_ = 0; }
  void releaseOwner(uint32_t owner) {
    binding_.releaseOwner(owner);
    if (owner == owner_) owner_ = 0;
  }
  uint32_t device() const { return binding_.device(); }
  uint32_t owner() const { return owner_; }
  bool hasPending() const { return binding_.hasPending(); }
  uint64_t accepted() const { return subscriptions_.accepted(); }
  uint64_t backpressure() const { return subscriptions_.backpressure(); }

 private:
  LocationPositionSubscriptions subscriptions_;
  LocationLeaseBinding<RuntimeDevices::Registry, RuntimeDevices::LeaseInfo> binding_;
  uint32_t owner_ = 0;
};

}  // namespace RuntimeStreams
