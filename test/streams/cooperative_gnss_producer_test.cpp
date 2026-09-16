#include "runtime/streams/CooperativeGnssProducer.h"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace RuntimeStreams;

namespace {
t5_gps_state_t fix(uint32_t age = 12) {
  t5_gps_state_t f{};
  f.status = T5_GPS_STATUS_FIX;
  f.fix_valid = f.receiver_detected = 1;
  f.latitude = 44.532385;
  f.longitude = -116.056066;
  f.age_ms = age;
  return f;
}
uint32_t little32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void consume(Registry& r, uint32_t owner, t5_stream_t stream, uint32_t expected) {
  uint8_t bytes[GnssRecordAdapter::Size]{};
  uint32_t n = 0;
  assert(r.readRecord(owner, stream, bytes, sizeof(bytes), &n) == T5_STREAM_OK);
  assert(n == sizeof(bytes));
  assert(little32(bytes + RISCRTE_FIX_OFFSET_SAMPLE_MS) == expected);
  assert(little32(bytes + RISCRTE_FIX_OFFSET_FIX_MS) == expected - 12);
}
}

int main() {
  Registry registry;
  LocationPositionSubscriptions subscriptions(registry);
  CooperativeGnssProducer producer;
  LocationPositionSubscriptions::Lease provider = 0, first = 0, second = 0;
  t5_stream_t a = 0, b = 0;
  assert(subscriptions.attachProvider(7, 99, &provider) == T5_STREAM_OK);
  assert(subscriptions.subscribe(provider, 8, &first, &a) == T5_STREAM_OK);
  assert(subscriptions.subscribe(provider, 9, &second, &b) == T5_STREAM_OK);
  auto observation = fix();
  assert(producer.retry(subscriptions, provider, 99) == T5_STREAM_AGAIN);
  for (uint32_t i = 0; i < 4; ++i)
    assert(producer.submit(subscriptions, provider, 99, observation, 2000 + i) == T5_STREAM_OK);
  assert(!producer.hasPending());
  assert(subscriptions.accepted() == 4 && subscriptions.deliveries() == 8);

  // A new observation cannot enter either full queue. Keep its original
  // timestamp and bytes, reject an attempt to overwrite it with a newer fix.
  assert(producer.submit(subscriptions, provider, 99, observation, 2004) == T5_STREAM_AGAIN);
  assert(producer.hasPending() && producer.blocked() == 1);
  assert(producer.submit(subscriptions, provider, 99, observation, 2005) == T5_STREAM_BUSY);
  consume(registry, 8, a, 2000);
  assert(producer.retry(subscriptions, provider, 99) == T5_STREAM_AGAIN);
  assert(producer.hasPending() && subscriptions.deliveries() == 8);
  consume(registry, 9, b, 2000);
  assert(producer.retry(subscriptions, provider, 99) == T5_STREAM_OK);
  assert(!producer.hasPending() && producer.retries() == 2);
  assert(subscriptions.accepted() == 5 && subscriptions.deliveries() == 10);
  for (uint32_t i = 1; i <= 4; ++i) {
    consume(registry, 8, a, 2000 + i);
    consume(registry, 9, b, 2000 + i);
  }

  // A cached fix read a millisecond later has the same acquisition time. It
  // is suppressed, not accidentally held as a backpressured pending sample.
  observation.age_ms = 13;
  assert(producer.submit(subscriptions, provider, 99, observation, 2005) == T5_STREAM_AGAIN);
  assert(!producer.hasPending() && subscriptions.duplicates() == 1);
  observation = fix();
  observation.fix_valid = 0;
  assert(producer.submit(subscriptions, provider, 99, observation, 2006) == T5_STREAM_AGAIN);
  assert(!producer.hasPending());
  observation = fix();
  assert(producer.submit(subscriptions, provider, 99, observation, 2010) == T5_STREAM_OK);
  consume(registry, 8, a, 2010);
  consume(registry, 9, b, 2010);

  // Fill again and simulate hot-unplug while the next observation is pending.
  for (uint32_t i = 0; i < 4; ++i)
    assert(producer.submit(subscriptions, provider, 99, observation, 2020 + i) == T5_STREAM_OK);
  assert(producer.submit(subscriptions, provider, 99, observation, 2024) == T5_STREAM_AGAIN);
  assert(producer.hasPending());
  assert(subscriptions.disconnect(provider) == T5_STREAM_OK);
  assert(producer.retry(subscriptions, provider, 99) == T5_STREAM_DISCONNECTED);
  assert(!producer.hasPending());
  for (uint32_t i = 0; i < 4; ++i) {
    consume(registry, 8, a, 2020 + i);
    consume(registry, 9, b, 2020 + i);
  }
  uint8_t bytes[GnssRecordAdapter::Size]{};
  uint32_t size = 0;
  assert(registry.readRecord(8, a, bytes, sizeof(bytes), &size) == T5_STREAM_DISCONNECTED);
  assert(registry.readRecord(9, b, bytes, sizeof(bytes), &size) == T5_STREAM_DISCONNECTED);
  assert(subscriptions.unsubscribe(8, first) == T5_STREAM_OK);
  assert(subscriptions.unsubscribe(9, second) == T5_STREAM_OK);
  LocationPositionSubscriptions::Lease replacement = 0;
  assert(subscriptions.attachProvider(11, 100, &replacement) == T5_STREAM_OK);
  assert(replacement != provider);
  assert(producer.submit(subscriptions, provider, 99, observation, 2030) == T5_STREAM_DISCONNECTED);
  producer.clear();
  registry.release(8); registry.release(9); registry.release(7); registry.release(11);
  std::cout << "Cooperative GNSS producer tests passed\n";
}
