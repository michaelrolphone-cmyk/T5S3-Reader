#include "runtime/capabilities/DeviceRegistry.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace RuntimeDevices;

static void expect(Registry& registry, uint64_t& cursor, EventKind kind,
                   DeviceHandle handle, State before, State after, uint32_t revoked,
                   const char* identity) {
  Event event{};
  assert(registry.poll(&cursor, &event) == PollResult::Next);
  assert(event.sequence == cursor);
  assert(event.kind == kind && event.device == handle);
  assert(event.previous == before && event.current == after);
  assert(event.revokedLeases == revoked);
  assert(std::strcmp(event.identity, identity) == 0);
}

int main() {
  Registry registry;
  Event event{};
  uint64_t cursor = 0;
  assert(registry.poll(&cursor, &event) == PollResult::Empty);
  assert(registry.poll(nullptr, &event) == PollResult::Invalid);
  assert(registry.poll(&cursor, nullptr) == PollResult::Invalid);
  uint64_t invalidCursor = 1;
  assert(registry.poll(&invalidCursor, &event) == PollResult::Invalid);

  const char* caps[] = {"location.position"};
  char identity[] = "gnss.logical.0";
  Descriptor desc{identity, "GNSS", "gps-nmea", Transport::Uart, caps, 1, 10};
  DeviceHandle gnss = 0, duplicate = 123;
  assert(registry.add(desc, State::Available, &gnss));
  std::strcpy(identity, "mutated");
  expect(registry, cursor, EventKind::Added, gnss, State::Removed,
         State::Available, 0, "gnss.logical.0");
  assert(registry.poll(&cursor, &event) == PollResult::Empty);
  const uint64_t atAdd = registry.cursor();
  desc.identity = "gnss.logical.0";
  assert(!registry.add(desc, State::Available, &duplicate) && !duplicate);
  assert(registry.cursor() == atAdd);  // Rejected changes emit nothing.

  LeaseHandle one = 0, two = 0;
  assert(registry.acquire("location.position", 41, &one) == Result::Ok);
  assert(registry.acquire("location.position", 42, &two) == Result::Ok);
  assert(registry.setState(gnss, State::Busy));
  assert(registry.valid(one, 41) && registry.valid(two, 42));
  expect(registry, cursor, EventKind::StateChanged, gnss, State::Available,
         State::Busy, 0, "gnss.logical.0");
  assert(registry.setState(gnss, State::Busy));
  assert(registry.poll(&cursor, &event) == PollResult::Empty);

  assert(registry.setState(gnss, State::Unavailable));
  assert(!registry.valid(one, 41) && !registry.valid(two, 42));
  assert(registry.leaseCount() == 0);
  expect(registry, cursor, EventKind::StateChanged, gnss, State::Busy,
         State::Unavailable, 2, "gnss.logical.0");
  expect(registry, cursor, EventKind::CapabilityLost, gnss, State::Busy,
         State::Unavailable, 2, "gnss.logical.0");
  assert(registry.setState(gnss, State::Unavailable));
  assert(registry.poll(&cursor, &event) == PollResult::Empty);
  assert(registry.setState(gnss, State::Available));
  expect(registry, cursor, EventKind::StateChanged, gnss, State::Unavailable,
         State::Available, 0, "gnss.logical.0");
  assert(registry.acquire("location.position", 41, &one) == Result::Ok);

  // Removal has an identity even after the handle becomes invalid.
  assert(registry.remove(gnss));
  DeviceInfo info{};
  assert(!registry.get(gnss, &info));
  expect(registry, cursor, EventKind::CapabilityLost, gnss, State::Available,
         State::Removed, 1, "gnss.logical.0");
  expect(registry, cursor, EventKind::Removed, gnss, State::Available,
         State::Removed, 1, "gnss.logical.0");
  assert(registry.poll(&cursor, &event) == PollResult::Empty);
  DeviceHandle replacement = 0;
  assert(registry.add(desc, State::Discovered, &replacement) && replacement != gnss);
  expect(registry, cursor, EventKind::Added, replacement, State::Removed,
         State::Discovered, 0, "gnss.logical.0");
  assert(registry.setState(replacement, State::Removed));
  expect(registry, cursor, EventKind::Removed, replacement, State::Discovered,
         State::Removed, 0, "gnss.logical.0");
  assert(!registry.get(replacement, &info));

  // Cursors are independent. Slow readers detect a gap instead of treating a
  // truncated event history as complete; they can enumerate and resubscribe.
  Registry burst;
  DeviceHandle sensor = 0;
  desc.identity = "sensor.logical.0";
  assert(burst.add(desc, State::Discovered, &sensor));
  const uint64_t early = burst.cursor();
  for (size_t i = 0; i < kMaxEvents + 9u; ++i) {
    assert(burst.setState(sensor, (i & 1u) ? State::Discovered : State::Identified));
  }
  assert(burst.cursor() == early + kMaxEvents + 9u);
  assert(burst.overwrittenEvents() == burst.cursor() - kMaxEvents);
  uint64_t slow = 0, lost = 0;
  event.sequence = UINT64_MAX;
  assert(burst.poll(&slow, &event, &lost) == PollResult::Gap);
  assert(lost == burst.cursor() - kMaxEvents && slow == lost);
  assert(event.sequence == UINT64_MAX);  // Gap never returns a partial event.
  assert(burst.get(sensor, &info) && info.state == State::Identified);
  for (size_t i = 0; i < kMaxEvents; ++i) {
    assert(burst.poll(&slow, &event) == PollResult::Next);
    assert(event.sequence == slow && event.device == sensor);
    assert(std::strcmp(event.identity, "sensor.logical.0") == 0);
  }
  assert(slow == burst.cursor());
  assert(burst.poll(&slow, &event) == PollResult::Empty);
  uint64_t fresh = burst.cursor();
  assert(burst.setState(sensor, State::Available));
  assert(burst.poll(&fresh, &event) == PollResult::Next);
  assert(event.kind == EventKind::StateChanged && event.current == State::Available);
  assert(burst.poll(&fresh, &event) == PollResult::Empty);
  std::puts("Unified device lifecycle journal and overflow recovery tests passed");
}
