#include "runtime/capabilities/DeviceRegistry.h"
#include "runtime/streams/LocationLeaseBinding.h"
#include "runtime/streams/LiveGnssSession.h"
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace RuntimeStreams;
using namespace RuntimeDevices;

namespace {
t5_gps_state_t fix() {
  t5_gps_state_t state{};
  state.status = T5_GPS_STATUS_FIX;
  state.fix_valid = state.receiver_detected = 1;
  state.latitude = 44.532385;
  state.longitude = -116.056066;
  state.age_ms = 12;
  return state;
}
void consume(RuntimeStreams::Registry& streams, uint32_t owner,
             t5_stream_t stream, uint32_t sample) {
  uint8_t bytes[GnssRecordAdapter::Size]{};
  uint32_t count = 0;
  assert(streams.readRecord(owner, stream, bytes, sizeof(bytes), &count) == T5_STREAM_OK);
  assert(count == sizeof(bytes));
  const uint32_t received = uint32_t(bytes[RISCRTE_FIX_OFFSET_SAMPLE_MS]) |
      (uint32_t(bytes[RISCRTE_FIX_OFFSET_SAMPLE_MS + 1]) << 8) |
      (uint32_t(bytes[RISCRTE_FIX_OFFSET_SAMPLE_MS + 2]) << 16) |
      (uint32_t(bytes[RISCRTE_FIX_OFFSET_SAMPLE_MS + 3]) << 24);
  assert(received == sample);
}
void sessionTest() {
  RuntimeDevices::Registry devices;
  RuntimeStreams::Registry streams;
  LiveGnssSession session(devices, streams); // Inject SAME stream registry.
  constexpr const char* caps[] = {"location.position"};
  const Descriptor descriptor{"board.gnss.uart0", "GNSS", "gps-nmea",
                              Transport::Uart, caps, 1, 100};
  DeviceHandle physical = 0;
  assert(devices.add(descriptor, State::Available, &physical));
  LeaseHandle source = 0;
  assert(devices.acquire("location.position", 101, &source, physical) == Result::Ok);
  assert(session.attach(101, physical, source) == T5_STREAM_OK);
  LiveGnssSession::Token consumer = 0;
  t5_stream_t stream = 0;
  assert(session.subscribe(202, &consumer, &stream) == T5_STREAM_OK);
  const auto observation = fix();
  assert(session.beforePoll(999) == LiveGnssSession::PollDecision::Denied);
  for (uint32_t i = 0; i < 4; ++i) {
    assert(session.beforePoll(101) == LiveGnssSession::PollDecision::Poll);
    assert(session.publishCopied(101, observation, 2000 + i) == T5_STREAM_OK);
  }
  assert(session.publishCopied(999, observation, 2004) == T5_STREAM_DENIED);
  assert(session.publishCopied(101, observation, 2004) == T5_STREAM_AGAIN);
  assert(session.hasPending() && session.backpressure() == 1);
  // A pending fix blocks UART polling, even when the next sample is available.
  assert(session.beforePoll(101) == LiveGnssSession::PollDecision::Backpressured);
  assert(session.hasPending());
  assert(session.publishCopied(101, observation, 3000) == T5_STREAM_BUSY);
  consume(streams, 202, stream, 2000);
  assert(session.beforePoll(101) == LiveGnssSession::PollDecision::Retried);
  assert(!session.hasPending() && session.accepted() == 5);
  for (uint32_t i = 1; i <= 4; ++i) consume(streams, 202, stream, 2000 + i);
  assert(session.beforePoll(101) == LiveGnssSession::PollDecision::Poll);
  assert(session.publishCopied(101, observation, 2010) == T5_STREAM_OK);
  session.disconnect(); // Must finish, drain, then report physical loss.
  assert(!session.owner() && !session.hasPending());
  consume(streams, 202, stream, 2010);
  uint8_t bytes[GnssRecordAdapter::Size]{};
  uint32_t size = 0;
  assert(streams.readRecord(202, stream, bytes, sizeof(bytes), &size) == T5_STREAM_DISCONNECTED);
  assert(session.unsubscribe(202, consumer) == T5_STREAM_OK);
  assert(devices.leaseCount() == 1); // Borrowed driver grant untouched.
  assert(devices.release(source, 101) == Result::Ok);
  streams.release(202);
}
}

int main() {
  RuntimeDevices::Registry devices;
  RuntimeStreams::Registry streams;
  LocationPositionSubscriptions subscriptions(streams);
  LocationLeaseBinding<RuntimeDevices::Registry, RuntimeDevices::LeaseInfo> binding(devices, subscriptions);
  constexpr const char* capabilities[] = {"location.position"};
  const Descriptor descriptor{"board.gnss.uart0", "GNSS", "gps-nmea",
                              Transport::Uart, capabilities, 1, 100};
  DeviceHandle physical = 0;
  assert(devices.add(descriptor, State::Available, &physical) && physical);
  LeaseHandle dependency = 0;
  assert(devices.acquire("location.position", 10, &dependency, physical, Mode::Dependency) == Result::Ok);
  LocationPositionSubscriptions::Lease source = 0, first = 0, second = 0;
  assert(binding.attach(10, physical, dependency, &source) == T5_STREAM_DENIED);
  assert(!source && !binding.provider());
  assert(devices.release(dependency, 10) == Result::Ok);

  LeaseHandle sourceGrant = 0;
  assert(devices.acquire("location.position", 10, &sourceGrant, physical) == Result::Ok);
  assert(sourceGrant && devices.valid(sourceGrant, 10));
  t5_stream_t a = 0, b = 0;
  assert(binding.attach(11, physical, sourceGrant, &source) == T5_STREAM_DENIED);
  assert(!source);
  assert(binding.attach(10, physical, sourceGrant, &source) == T5_STREAM_OK);
  assert(binding.subscribe(21, &first, &a) == T5_STREAM_OK);
  assert(binding.subscribe(22, &second, &b) == T5_STREAM_OK);
  assert(devices.leaseCount() == 3);
  assert(streams.writeRecord(21, a, "spoof", 5) == T5_STREAM_DENIED);
  const auto observation = fix();
  for (uint32_t i = 0; i != 4; ++i)
    assert(binding.submit(10, observation, 1000 + i) == T5_STREAM_OK);
  assert(binding.submit(10, observation, 1004) == T5_STREAM_AGAIN);
  assert(binding.hasPending());
  assert(devices.releaseOwner(22) == 1);
  assert(binding.reconcile());
  assert(devices.leaseCount() == 2 && subscriptions.subscribers() == 1);
  assert(binding.unsubscribe(22, second) == T5_STREAM_INVALID);
  uint8_t bytes[GnssRecordAdapter::Size]{};
  uint32_t count = 0;
  assert(streams.readRecord(22, b, bytes, sizeof(bytes), &count) == T5_STREAM_INVALID);
  assert(binding.retry(11) == T5_STREAM_DENIED);
  assert(binding.retry(10) == T5_STREAM_AGAIN && binding.hasPending());
  consume(streams, 21, a, 1000);
  assert(binding.retry(10) == T5_STREAM_OK && !binding.hasPending());
  for (uint32_t i = 1; i != 5; ++i) consume(streams, 21, a, 1000 + i);
  assert(binding.submit(10, observation, 1010) == T5_STREAM_OK);
  const uint64_t beforeLoss = devices.cursor();
  assert(devices.setState(physical, State::Unavailable));
  assert(devices.leaseCount() == 0);
  uint64_t cursor = beforeLoss;
  Event event{};
  bool sawLoss = false;
  while (devices.poll(&cursor, &event) == PollResult::Next)
    if (event.kind == EventKind::CapabilityLost && event.device == physical)
      sawLoss = true;
  assert(sawLoss);
  assert(!binding.reconcile() && !binding.hasPending() && !binding.provider());
  assert(binding.submit(10, observation, 1011) == T5_STREAM_DENIED);
  consume(streams, 21, a, 1010);
  assert(streams.readRecord(21, a, bytes, sizeof(bytes), &count) == T5_STREAM_DISCONNECTED);
  assert(binding.unsubscribe(21, first) == T5_STREAM_OK);
  assert(devices.remove(physical));
  DeviceHandle replacement = 0;
  assert(devices.add(descriptor, State::Available, &replacement));
  assert(replacement != physical);
  LeaseHandle replacementGrant = 0;
  assert(devices.acquire("location.position", 12, &replacementGrant, replacement) == Result::Ok);
  assert(binding.attach(12, physical, replacementGrant, &source) == T5_STREAM_DENIED);
  assert(binding.attach(12, replacement, replacementGrant, &source) == T5_STREAM_OK);
  assert(binding.subscribe(24, &first, &a) == T5_STREAM_OK);
  binding.deviceLost(physical);
  assert(binding.provider());
  binding.releaseOwner(24);
  binding.releaseOwner(12);
  assert(devices.leaseCount() == 1);
  assert(devices.release(replacementGrant, 12) == Result::Ok);
  streams.release(10);
  streams.release(12);
  streams.release(21);
  streams.release(22);
  streams.release(24);
  sessionTest();
  std::cout << "Production device registry GNSS lease/stream integration passed\n";
}
