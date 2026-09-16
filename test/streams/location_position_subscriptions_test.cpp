#include "runtime/streams/LocationPositionSubscriptions.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace RuntimeStreams;

namespace {
t5_gps_state_t fix() {
  t5_gps_state_t result{};
  result.status = T5_GPS_STATUS_FIX;
  result.fix_valid = 1;
  result.receiver_detected = 1;
  result.latitude = 44.532385;
  result.longitude = -116.056066;
  result.age_ms = 12;
  result.satellites = 7;
  return result;
}
uint32_t little32(const uint8_t* data) {
  return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
         (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}
void readFix(Registry& registry, uint32_t owner, t5_stream_t stream, uint32_t expectedMs) {
  uint8_t payload[GnssRecordAdapter::Size]{};
  uint32_t count = 999;
  assert(registry.readRecord(owner, stream, payload, sizeof(payload) - 1, &count) == T5_STREAM_LIMIT);
  assert(count == 0); // Short reads never consume any prefix.
  assert(registry.readRecord(owner, stream, payload, sizeof(payload), &count) == T5_STREAM_OK);
  assert(count == sizeof(payload));
  assert(little32(payload + RISCRTE_FIX_OFFSET_VERSION) == RISCRTE_LOCATION_FIX_VERSION);
  assert(little32(payload + RISCRTE_FIX_OFFSET_SAMPLE_MS) == expectedMs);
  assert(little32(payload + RISCRTE_FIX_OFFSET_FIX_MS) == expectedMs - 12);
  assert(little32(payload + RISCRTE_FIX_OFFSET_FLAGS) == 0); // No invented optional observations.
}
}

int main() {
  Registry registry;
  LocationPositionSubscriptions service(registry);
  LocationPositionSubscriptions::Lease provider = 123, a = 0, b = 0;
  t5_stream_t sa = 123, sb = 0;
  assert(service.attachProvider(0, 50, &provider) == T5_STREAM_INVALID && provider == 0);
  assert(service.attachProvider(9, 50, &provider) == T5_STREAM_OK && provider);
  LocationPositionSubscriptions::Lease busy = 33;
  assert(service.attachProvider(10, 51, &busy) == T5_STREAM_BUSY && busy == 0);
  assert(service.subscribe(provider + 1, 101, &a, &sa) == T5_STREAM_DISCONNECTED && !a && !sa);
  assert(service.subscribe(provider, 101, &a, &sa) == T5_STREAM_OK && a && sa);
  assert(service.subscribe(provider, 102, &b, &sb) == T5_STREAM_OK && b && sb && sa != sb);
  assert(service.subscribers() == 2);
  assert(registry.writeRecord(101, sa, "fake", 4) == T5_STREAM_DENIED);
  assert(registry.writeRecord(102, sb, "fake", 4) == T5_STREAM_DENIED);
  assert(registry.readRecord(102, sa, nullptr, 0, &busy) == T5_STREAM_INVALID);

  const auto gps = fix();
  for (uint32_t i = 0; i < 4; ++i)
    assert(service.publish(provider, 50, gps, 1000 + i) == T5_STREAM_OK);
  assert(service.accepted() == 4 && service.deliveries() == 8);
  assert(service.publish(provider, 50, gps, 1004) == T5_STREAM_AGAIN);
  assert(service.backpressure() == 1 && service.deliveries() == 8);
  readFix(registry, 101, sa, 1000);
  // A fast consumer cannot receive a duplicate when the other remains full.
  assert(service.publish(provider, 50, gps, 1004) == T5_STREAM_AGAIN);
  assert(service.accepted() == 4 && service.deliveries() == 8);
  readFix(registry, 102, sb, 1000);
  assert(service.publish(provider, 50, gps, 1004) == T5_STREAM_OK);
  assert(service.accepted() == 5 && service.deliveries() == 10);
  assert(service.publish(provider, 51, gps, 1005) == T5_STREAM_DISCONNECTED);
  assert(service.disconnect(provider) == T5_STREAM_OK);
  assert(service.publish(provider, 50, gps, 1005) == T5_STREAM_DISCONNECTED);
  assert(service.subscribe(provider, 103, &busy, &sa) == T5_STREAM_DISCONNECTED);
  assert(service.attachProvider(10, 51, &busy) == T5_STREAM_BUSY && busy == 0);
  for (uint32_t i = 1; i <= 4; ++i) {
    readFix(registry, 101, sa, 1000 + i);
    readFix(registry, 102, sb, 1000 + i);
  }
  uint8_t payload[GnssRecordAdapter::Size]{};
  uint32_t count = 123;
  assert(registry.readRecord(101, sa, payload, sizeof(payload), &count) == T5_STREAM_DISCONNECTED && !count);
  assert(service.unsubscribe(102, a) == T5_STREAM_DENIED);
  assert(service.unsubscribe(101, a) == T5_STREAM_OK);
  assert(service.unsubscribe(101, a) == T5_STREAM_INVALID);
  assert(registry.readRecord(101, sa, payload, sizeof(payload), &count) == T5_STREAM_INVALID);
  assert(service.unsubscribe(102, b) == T5_STREAM_OK);
  assert(service.subscribers() == 0);

  LocationPositionSubscriptions::Lease replacement = 0, c = 0;
  t5_stream_t sc = 0;
  assert(service.attachProvider(10, 51, &replacement) == T5_STREAM_OK && replacement != provider);
  assert(service.publish(provider, 50, gps, 1010) == T5_STREAM_DISCONNECTED);
  assert(service.subscribe(replacement, 103, &c, &sc) == T5_STREAM_OK);
  auto invalid = gps;
  invalid.age_ms = 5001;
  assert(service.publish(replacement, 51, invalid, 1020) == T5_STREAM_AGAIN);
  assert(registry.readRecord(103, sc, payload, sizeof(payload), &count) == T5_STREAM_AGAIN);
  assert(service.publish(replacement, 51, gps, 1021) == T5_STREAM_OK);
  service.releaseOwner(103);
  assert(service.subscribers() == 0);
  assert(registry.readRecord(103, sc, payload, sizeof(payload), &count) == T5_STREAM_INVALID);
  assert(service.unsubscribe(103, c) == T5_STREAM_INVALID);

  // A consumer can close its public handle; publication prunes that stale
  // subscription and must never start writing into a reused registry slot.
  LocationPositionSubscriptions::Lease d = 0;
  t5_stream_t sd = 0;
  assert(service.subscribe(replacement, 104, &d, &sd) == T5_STREAM_OK);
  assert(registry.close(104, sd) == T5_STREAM_OK);
  assert(service.publish(replacement, 51, gps, 1022) == T5_STREAM_OK);
  assert(service.subscribers() == 0);
  assert(service.unsubscribe(104, d) == T5_STREAM_INVALID);

  // Provider termination, even without an explicit unsubscribe, terminates
  // its subscriber's queue and invalidates the source lease.
  assert(service.subscribe(replacement, 105, &d, &sd) == T5_STREAM_OK);
  assert(service.publish(replacement, 51, gps, 1030) == T5_STREAM_OK);
  service.releaseOwner(10);
  readFix(registry, 105, sd, 1030);
  assert(registry.readRecord(105, sd, payload, sizeof(payload), &count) == T5_STREAM_DISCONNECTED);
  assert(service.publish(replacement, 51, gps, 1031) == T5_STREAM_DISCONNECTED);
  service.releaseOwner(105);
  registry.release(105);
  assert(service.subscribers() == 0);
  assert(service.attachProvider(9, 52, &provider) == T5_STREAM_OK && provider != replacement);
  registry.release(101);
  registry.release(102);
  registry.release(103);
  registry.release(104);
  registry.release(9);
  std::cout << "Location position subscription lifecycle tests passed\n";
}
