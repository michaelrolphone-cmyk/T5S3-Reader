#pragma once

#include "LocationPositionSubscriptions.h"
#include <cstdint>

namespace RuntimeStreams {

// Firmware-only producer state for a GPS driver whose ABI permits polling only
// on the claiming task. The owner calls submit() with a COPY after hardware
// polling, then stops polling new observations while hasPending() is true and
// calls retry() instead. Neither function polls hardware or stores ELF code
// pointers; the caller serializes them with subscription operations using the
// shared stream-registry mutex. An eventual driver worker must own its task,
// driver, semantic device lease and this state together.
class CooperativeGnssProducer final {
 public:
  using Lease = LocationPositionSubscriptions::Lease;

  // Only a queue-full result is retained; no-fix and repeated already accepted
  // observations also return AGAIN but must not become a blocking pending fix.
  int32_t submit(LocationPositionSubscriptions& subscriptions, Lease provider,
                 uint32_t deviceEpoch, const t5_gps_state_t& observation,
                 uint32_t sampleMs, uint32_t verifiedFields = 0) {
    if (pending_) return T5_STREAM_BUSY;
    const uint64_t before = subscriptions.backpressure();
    const int32_t result = subscriptions.publish(provider, deviceEpoch, observation,
                                                 sampleMs, verifiedFields);
    if (result == T5_STREAM_AGAIN && subscriptions.backpressure() != before) {
      observation_ = observation; // By value: never retain an ELF-owned pointer.
      sampleMs_ = sampleMs;
      verifiedFields_ = verifiedFields;
      pending_ = true;
      ++blocked_;
    }
    return result;
  }

  int32_t retry(LocationPositionSubscriptions& subscriptions, Lease provider,
                uint32_t deviceEpoch) {
    if (!pending_) return T5_STREAM_AGAIN;
    ++retries_;
    const uint64_t before = subscriptions.backpressure();
    const int32_t result = subscriptions.publish(provider, deviceEpoch, observation_,
                                                 sampleMs_, verifiedFields_);
    // A successful retry has reached every subscriber atomically. If it is
    // already accepted, suppress its duplicate; on loss or terminal errors,
    // clear state rather than blocking a replacement provider forever.
    if (result != T5_STREAM_AGAIN || subscriptions.backpressure() == before)
      clear();
    return result;
  }

  bool hasPending() const { return pending_; }
  uint64_t blocked() const { return blocked_; }
  uint64_t retries() const { return retries_; }

  // Invoke when the device lease is revoked or before unloading the driver.
  // An unaccepted pending observation is not silently reported as delivered.
  void clear() {
    pending_ = false;
    observation_ = {};
    sampleMs_ = 0;
    verifiedFields_ = 0;
  }

 private:
  t5_gps_state_t observation_{};
  uint32_t sampleMs_ = 0, verifiedFields_ = 0;
  bool pending_ = false;
  uint64_t blocked_ = 0, retries_ = 0;
};

} // namespace RuntimeStreams
