#pragma once

#include "GnssRecordAdapter.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RuntimeStreams {

// Firmware-only data-plane coordinator. The Unified Device Registry / resolver
// must validate a context's location.position capability lease BEFORE calling
// subscribe(). No method in this class is exported directly to an ELF.
//
// All calls, including registry reads/closes and scheduler pump(), must be made
// under the SAME external registry mutex. This permits an all-or-nothing
// BLOCK_PRODUCER preflight without adding another lock or retaining callbacks
// into a driver ELF. The driver is polled on its owning task, outside that lock;
// publish() accepts only a copied observation and never accesses hardware.
class LocationPositionSubscriptions final {
 public:
  static constexpr uint32_t MaxSubscribers = 4;
  static constexpr uint32_t QueueRecords = 4;
  using Lease = uint64_t;

  explicit LocationPositionSubscriptions(Registry& registry) : registry_(registry) {}
  LocationPositionSubscriptions(const LocationPositionSubscriptions&) = delete;
  LocationPositionSubscriptions& operator=(const LocationPositionSubscriptions&) = delete;

  // The caller proves the provider has acquired the semantic device capability.
  // deviceEpoch is the generation of the resolved physical device, not a clock.
  // No new provider can replace one with still-live old subscriptions.
  int32_t attachProvider(uint32_t providerOwner, uint32_t deviceEpoch, Lease* out) {
    if (out) *out = 0;
    if (!out || !providerOwner || !deviceEpoch) return T5_STREAM_INVALID;
    if (source_.active || subscribers()) return T5_STREAM_BUSY;
    if (sourceGeneration_ == std::numeric_limits<uint32_t>::max()) return T5_STREAM_LIMIT;
    source_ = {providerOwner, deviceEpoch, ++sourceGeneration_, true};
    lastAccepted_ = false; // A replacement receiver is a new observation epoch.
    lastRecord_.fill(0);
    *out = (static_cast<uint64_t>(source_.generation) << 32) | providerOwner;
    return T5_STREAM_OK;
  }

  // Invoke ONLY after the device resolver has authorized subscriberOwner and
  // its capability lease for this provider/device generation. This method
  // creates a distinct READ-only stream in the existing shared Registry.
  int32_t subscribe(Lease provider, uint32_t subscriberOwner, Lease* outLease,
                    t5_stream_t* outStream) {
    if (outLease) *outLease = 0;
    if (outStream) *outStream = 0;
    if (!outLease || !outStream || !subscriberOwner) return T5_STREAM_INVALID;
    if (!matches(provider)) return T5_STREAM_DISCONNECTED;
    for (unsigned i = 0; i < MaxSubscribers; ++i) {
      if (slots_[i].lease) continue;
      if (subscriptionGeneration_ == std::numeric_limits<uint32_t>::max()) return T5_STREAM_LIMIT;
      t5_stream_t stream = 0;
      const int32_t result = GnssRecordAdapter::open(registry_, subscriberOwner, &stream, QueueRecords);
      if (result != T5_STREAM_OK) return result;
      const Lease grant = (static_cast<Lease>(++subscriptionGeneration_) << 32) | (i + 1u);
      slots_[i] = {grant, subscriberOwner, stream};
      *outLease = grant;
      *outStream = stream;
      return T5_STREAM_OK;
    }
    return T5_STREAM_LIMIT;
  }

  // BLOCK_PRODUCER fanout: a full consumer stalls the ENTIRE publication.
  // Nothing is delivered to any subscriber until every live subscriber can
  // accept the record. This avoids duplicate delivery when the caller retries.
  // Repeated polling of the same GNSS observation also must not fill queues:
  // ignore only an observation that was PREVIOUSLY ACCEPTED. The sample_ms
  // field is not part of observation identity; fix_ms, coordinates, validity
  // flags and optional measurements ARE. A previously backpressured record is
  // never marked accepted, so it remains eligible for an exact retry.
  // An externally closed/finished consumer is pruned; its old handle is never
  // reused, and stale subscription leases cannot release a replacement.
  int32_t publish(Lease provider, uint32_t currentDeviceEpoch,
                  const t5_gps_state_t& observation, uint32_t sampleMs,
                  uint32_t verifiedFields = 0) {
    if (!matches(provider) || !currentDeviceEpoch ||
        currentDeviceEpoch != source_.deviceEpoch) return T5_STREAM_DISCONNECTED;
    uint8_t record[GnssRecordAdapter::Size]{};
    if (!GnssRecordAdapter::encode(observation, sampleMs, record, verifiedFields))
      return T5_STREAM_AGAIN;
    // Exclude only the changing sample timestamp (offset 4..7). Comparing the
    // remaining complete wire representation includes fix time and prevents
    // stale reads of a cached fix from being published as fresh observations.
    if (lastAccepted_ &&
        std::memcmp(record, lastRecord_.data(), RISCRTE_FIX_OFFSET_SAMPLE_MS) == 0 &&
        std::memcmp(record + RISCRTE_FIX_OFFSET_FIX_MS,
                    lastRecord_.data() + RISCRTE_FIX_OFFSET_FIX_MS,
                    sizeof(record) - RISCRTE_FIX_OFFSET_FIX_MS) == 0) {
      ++duplicates_;
      return T5_STREAM_AGAIN;
    }
    for (auto& slot : slots_) {
      if (!slot.lease) continue;
      char schema[RecordQueue::MaxSchema]{};
      RecordQueue::Stats stats{};
      const int32_t result = registry_.recordInfo(slot.owner, slot.stream, schema, sizeof(schema), &stats);
      if (result == T5_STREAM_INVALID) { slot = {}; continue; } // Already closed by its context.
      if (result != T5_STREAM_OK) return result;
      if (stats.terminal) {
        (void)registry_.close(slot.owner, slot.stream);
        slot = {};
        continue;
      }
      if (std::strcmp(schema, RISCRTE_LOCATION_FIX_SCHEMA) || stats.max_record < sizeof(record))
        return T5_STREAM_UNSUPPORTED;
      if (stats.queued_records >= stats.capacity_records) {
        ++backpressure_;
        return T5_STREAM_AGAIN;
      }
    }
    // The bridge mutex excludes concurrent reads, app close, and pipe pump
    // between capacity preflight and these bounded atomic writes.
    for (const auto& slot : slots_) {
      if (!slot.lease) continue;
      const int32_t result = registry_.produceRecord(slot.owner, slot.stream, record, sizeof(record));
      if (result != T5_STREAM_OK) return result; // Contract violation without external serialization.
      ++deliveries_;
    }
    std::memcpy(lastRecord_.data(), record, sizeof(record));
    lastAccepted_ = true;
    ++accepted_;
    return T5_STREAM_OK;
  }

  // Device loss finishes every subscriber's queue. Already accepted fixes
  // remain readable, then DISCONNECTED is returned. An old provider cannot
  // resume even if an identical receiver appears with a new device epoch.
  int32_t disconnect(Lease provider) {
    if (!matches(provider)) return T5_STREAM_INVALID;
    source_.active = false;
    for (auto& slot : slots_) {
      if (!slot.lease) continue;
      const int32_t result = registry_.finish(slot.owner, slot.stream, T5_STREAM_DISCONNECTED);
      if (result == T5_STREAM_INVALID) slot = {}; // Owner already reclaimed it.
    }
    return T5_STREAM_OK;
  }

  // Query ownership AND the actual registry stream under the same external
  // mutex. A public stream close() may occur without semantic unsubscribe;
  // the binding uses this to release the orphaned device capability grant.
  // A terminal stream is no longer an active hardware subscription, although
  // its already-queued records may remain readable by its owner.
  bool streamOpen(uint32_t owner, Lease lease) {
    if (!owner || !lease) return false;
    for (const auto& slot : slots_) {
      if (slot.lease != lease || slot.owner != owner) continue;
      char schema[RecordQueue::MaxSchema]{};
      RecordQueue::Stats stats{};
      return registry_.recordInfo(owner, slot.stream, schema, sizeof(schema), &stats) == T5_STREAM_OK &&
             !stats.terminal && std::strcmp(schema, RISCRTE_LOCATION_FIX_SCHEMA) == 0;
    }
    return false;
  }

  int32_t unsubscribe(uint32_t subscriberOwner, Lease lease) {
    if (!subscriberOwner || !lease) return T5_STREAM_INVALID;
    for (auto& slot : slots_) {
      if (slot.lease != lease) continue;
      if (slot.owner != subscriberOwner) return T5_STREAM_DENIED;
      (void)registry_.close(slot.owner, slot.stream);
      slot = {};
      return T5_STREAM_OK;
    }
    return T5_STREAM_INVALID;
  }

  // Call BEFORE Registry::release(owner). Reclaims both the provider side and
  // app-side subscriptions, including exceptional app unload without close().
  void releaseOwner(uint32_t owner) {
    if (!owner) return;
    if (source_.owner == owner && source_.active)
      (void)disconnect(providerLease());
    for (auto& slot : slots_) if (slot.lease && slot.owner == owner) {
      (void)registry_.close(slot.owner, slot.stream);
      slot = {};
    }
  }

  uint32_t subscribers() const {
    uint32_t count = 0;
    for (const auto& slot : slots_) if (slot.lease) ++count;
    return count;
  }
  uint64_t accepted() const { return accepted_; }
  uint64_t deliveries() const { return deliveries_; }
  uint64_t backpressure() const { return backpressure_; }
  uint64_t duplicates() const { return duplicates_; }

 private:
  struct Source { uint32_t owner = 0, deviceEpoch = 0, generation = 0; bool active = false; };
  struct Subscriber { Lease lease = 0; uint32_t owner = 0; t5_stream_t stream = 0; };
  Registry& registry_;
  Source source_{};
  std::array<Subscriber, MaxSubscribers> slots_{};
  std::array<uint8_t, GnssRecordAdapter::Size> lastRecord_{};
  bool lastAccepted_ = false;
  uint32_t sourceGeneration_ = 0, subscriptionGeneration_ = 0;
  uint64_t accepted_ = 0, deliveries_ = 0, backpressure_ = 0, duplicates_ = 0;
  Lease providerLease() const {
    return (static_cast<Lease>(source_.generation) << 32) | source_.owner;
  }
  bool matches(Lease provider) const {
    return provider && source_.active && provider == providerLease();
  }
};

} // namespace RuntimeStreams
