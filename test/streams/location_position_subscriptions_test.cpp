#include "runtime/streams/LocationPositionSubscriptions.h"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace RuntimeStreams;
namespace {
t5_gps_state_t fix() {
  t5_gps_state_t f{};
  f.status = T5_GPS_STATUS_FIX; f.fix_valid = 1; f.receiver_detected = 1;
  f.latitude = 44.532385; f.longitude = -116.056066; f.age_ms = 12; f.satellites = 7;
  return f;
}
uint32_t u32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void readFix(Registry& r, uint32_t owner, t5_stream_t stream, uint32_t time) {
  uint8_t payload[GnssRecordAdapter::Size]{};
  uint32_t count = 99;
  assert(r.readRecord(owner, stream, payload, sizeof(payload) - 1, &count) == T5_STREAM_LIMIT && !count);
  assert(r.readRecord(owner, stream, payload, sizeof(payload), &count) == T5_STREAM_OK && count == sizeof(payload));
  assert(u32(payload + RISCRTE_FIX_OFFSET_VERSION) == RISCRTE_LOCATION_FIX_VERSION);
  assert(u32(payload + RISCRTE_FIX_OFFSET_SAMPLE_MS) == time);
  assert(u32(payload + RISCRTE_FIX_OFFSET_FIX_MS) == time - 12);
  assert(u32(payload + RISCRTE_FIX_OFFSET_FLAGS) == 0);
}
}
int main() {
  Registry r;
  LocationPositionSubscriptions s(r);
  LocationPositionSubscriptions::Lease provider = 77, a = 0, b = 0, other = 99;
  t5_stream_t sa = 99, sb = 0;
  assert(s.attachProvider(0, 50, &provider) == T5_STREAM_INVALID && !provider);
  assert(s.attachProvider(9, 50, &provider) == T5_STREAM_OK && provider);
  assert(s.attachProvider(10, 51, &other) == T5_STREAM_BUSY && !other);
  assert(s.subscribe(provider + 1, 101, &a, &sa) == T5_STREAM_DISCONNECTED && !a && !sa);
  assert(s.subscribe(provider, 101, &a, &sa) == T5_STREAM_OK && a && sa);
  assert(s.subscribe(provider, 102, &b, &sb) == T5_STREAM_OK && b && sb && sa != sb);
  assert(s.subscribers() == 2);
  assert(r.writeRecord(101, sa, "spoof", 5) == T5_STREAM_DENIED);
  assert(r.writeRecord(102, sb, "spoof", 5) == T5_STREAM_DENIED);
  uint32_t n = 0;
  assert(r.readRecord(102, sa, nullptr, 0, &n) == T5_STREAM_INVALID);
  auto gps = fix();
  for (uint32_t i = 0; i < 4; ++i)
    assert(s.publish(provider, 50, gps, 1000 + i) == T5_STREAM_OK);
  assert(s.accepted() == 4 && s.deliveries() == 8);
  // Re-polling the same cached receiver fix changes sample time and age by
  // the same amount. This must not fill another slot or masquerade as
  // backpressure while both subscriber queues are full.
  gps.age_ms = 13;
  assert(s.publish(provider, 50, gps, 1004) == T5_STREAM_AGAIN);
  assert(s.duplicates() == 1 && s.backpressure() == 0 && s.accepted() == 4);
  gps = fix();
  assert(s.publish(provider, 50, gps, 1004) == T5_STREAM_AGAIN && s.backpressure() == 1);
  readFix(r, 101, sa, 1000);
  assert(s.publish(provider, 50, gps, 1004) == T5_STREAM_AGAIN);
  assert(s.accepted() == 4 && s.deliveries() == 8); // No duplicated fast-consumer record.
  readFix(r, 102, sb, 1000);
  assert(s.publish(provider, 50, gps, 1004) == T5_STREAM_OK);
  assert(s.accepted() == 5 && s.deliveries() == 10);
  gps.age_ms = 13;
  assert(s.publish(provider, 50, gps, 1005) == T5_STREAM_AGAIN);
  assert(s.duplicates() == 2 && s.backpressure() == 2 && s.deliveries() == 10);
  gps = fix();
  assert(s.publish(provider, 51, gps, 1005) == T5_STREAM_DISCONNECTED);
  assert(s.disconnect(provider) == T5_STREAM_OK);
  t5_stream_t denied = 99;
  assert(s.subscribe(provider, 103, &other, &denied) == T5_STREAM_DISCONNECTED && !denied);
  assert(s.attachProvider(10, 51, &other) == T5_STREAM_BUSY && !other);
  for (uint32_t i = 1; i <= 4; ++i) {
    readFix(r, 101, sa, 1000 + i);
    readFix(r, 102, sb, 1000 + i);
  }
  uint8_t payload[GnssRecordAdapter::Size]{};
  assert(r.readRecord(101, sa, payload, sizeof(payload), &n) == T5_STREAM_DISCONNECTED && !n);
  assert(s.unsubscribe(102, a) == T5_STREAM_DENIED);
  assert(s.unsubscribe(101, a) == T5_STREAM_OK);
  assert(s.unsubscribe(101, a) == T5_STREAM_INVALID);
  assert(r.readRecord(101, sa, payload, sizeof(payload), &n) == T5_STREAM_INVALID);
  assert(s.unsubscribe(102, b) == T5_STREAM_OK && s.subscribers() == 0);
  LocationPositionSubscriptions::Lease newer = 0, c = 0;
  t5_stream_t sc = 0;
  assert(s.attachProvider(10, 51, &newer) == T5_STREAM_OK && newer != provider);
  assert(s.publish(provider, 50, gps, 1010) == T5_STREAM_DISCONNECTED);
  assert(s.subscribe(newer, 103, &c, &sc) == T5_STREAM_OK);
  gps.age_ms = 5001;
  assert(s.publish(newer, 51, gps, 1020) == T5_STREAM_AGAIN);
  assert(r.readRecord(103, sc, payload, sizeof(payload), &n) == T5_STREAM_AGAIN);
  gps = fix();
  assert(s.publish(newer, 51, gps, 1021) == T5_STREAM_OK);
  gps.age_ms = 13;
  assert(s.publish(newer, 51, gps, 1022) == T5_STREAM_AGAIN && s.duplicates() == 3);
  gps = fix();
  s.releaseOwner(103);
  assert(s.subscribers() == 0 && r.readRecord(103, sc, payload, sizeof(payload), &n) == T5_STREAM_INVALID);
  assert(s.unsubscribe(103, c) == T5_STREAM_INVALID);
  LocationPositionSubscriptions::Lease d = 0;
  t5_stream_t sd = 0;
  assert(s.subscribe(newer, 104, &d, &sd) == T5_STREAM_OK);
  assert(r.close(104, sd) == T5_STREAM_OK);
  assert(s.publish(newer, 51, gps, 1022) == T5_STREAM_OK && !s.subscribers());
  assert(s.unsubscribe(104, d) == T5_STREAM_INVALID);
  assert(s.subscribe(newer, 105, &d, &sd) == T5_STREAM_OK);
  assert(s.publish(newer, 51, gps, 1030) == T5_STREAM_OK);
  s.releaseOwner(10);
  readFix(r, 105, sd, 1030);
  assert(r.readRecord(105, sd, payload, sizeof(payload), &n) == T5_STREAM_DISCONNECTED);
  assert(s.publish(newer, 51, gps, 1031) == T5_STREAM_DISCONNECTED);
  s.releaseOwner(105);
  r.release(105);
  assert(s.attachProvider(9, 52, &provider) == T5_STREAM_OK && provider != newer);
  r.release(101); r.release(102); r.release(103); r.release(104); r.release(9);
  std::cout << "Location position subscription lifecycle tests passed\n";
}
