#include "runtime/capabilities/DeviceEventSubscriptions.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

int main() {
  Registry registry;
  EventSubscriptions subscriptions(registry);
  const char* capabilities[] = {"location.position"};
  Descriptor gnss{"board.gnss", "GNSS", "gps-nmea", Transport::Uart,
                  capabilities, 1, 10};
  DeviceHandle first = 0;
  assert(registry.add(gnss, State::Discovered, &first));
  SubscriptionHandle one = 99;
  ExecutionContext owner, nextOwner;
  assert(subscriptions.subscribe(owner, &one) == ObserveResult::Denied && one == 0);
  assert(owner.begin());
  const uint32_t invocation = owner.id();
  assert(subscriptions.subscribe(owner, nullptr) == ObserveResult::Invalid);
  assert(subscriptions.subscribe(owner, &one) == ObserveResult::Ok && one);
  assert(subscriptions.count() == 1);
  SubscriptionHandle second = 0;
  assert(subscriptions.subscribe(owner, &second) == ObserveResult::Ok && second != one);
  assert(subscriptions.count() == 2);
  Event event{};
  uint64_t missed = 19;
  assert(subscriptions.poll(one, invocation, nullptr) == ObserveResult::Invalid);
  assert(subscriptions.poll(one, invocation + 1, &event) == ObserveResult::Denied);
  assert(subscriptions.unsubscribe(one, invocation + 1) == ObserveResult::Denied);
  assert(subscriptions.snapshot(second, invocation + 1, nullptr, 0, nullptr) ==
         ObserveResult::Invalid);
  assert(subscriptions.poll(0, invocation, &event) == ObserveResult::Stale);
  assert(subscriptions.poll(one, invocation, &event, &missed) == ObserveResult::Empty);
  assert(missed == 0);  // Existing events are not replayed to a new subscriber.

  size_t needed = 0;
  assert(subscriptions.snapshot(one, invocation, nullptr, 0, &needed) ==
         ObserveResult::Limit && needed == 1);
  DeviceInfo inventory[kMaxDevices]{};
  assert(subscriptions.snapshot(one, invocation, inventory, kMaxDevices, &needed) ==
         ObserveResult::Ok && needed == 1 && inventory[0].handle == first);
  assert(subscriptions.snapshot(second, invocation, inventory, kMaxDevices, &needed) ==
         ObserveResult::Ok && needed == 1);

  assert(registry.setState(first, State::Available));
  assert(subscriptions.poll(one, invocation, &event) == ObserveResult::Next);
  assert(event.kind == EventKind::StateChanged && event.device == first);
  assert(subscriptions.poll(one, invocation, &event) == ObserveResult::Empty);
  assert(subscriptions.poll(second, invocation, &event) == ObserveResult::Next &&
         event.kind == EventKind::StateChanged);  // Independent cursors.
  assert(subscriptions.unsubscribe(one, invocation) == ObserveResult::Ok);
  assert(subscriptions.poll(one, invocation, &event) == ObserveResult::Stale);
  SubscriptionHandle replacement = 0;
  assert(subscriptions.subscribe(owner, &replacement) == ObserveResult::Ok);
  assert(replacement != one);  // Recycled slot cannot alias an old handle.
  assert(subscriptions.unsubscribe(replacement, invocation) == ObserveResult::Ok);

  // A slow subscriber must resnapshot after journal overwrite; repeated poll
  // calls cannot silently drain a partial tail and present it as complete.
  for (size_t i = 0; i < kMaxEvents + 5; ++i)
    assert(registry.setState(first, (i & 1u) ? State::Available : State::Busy));
  assert(subscriptions.poll(second, invocation, &event, &missed) == ObserveResult::Gap);
  assert(missed > 0);
  assert(subscriptions.poll(second, invocation, &event) == ObserveResult::Gap);
  assert(subscriptions.snapshot(second, invocation, nullptr, 0, &needed) ==
         ObserveResult::Limit && needed == 1);
  assert(subscriptions.poll(second, invocation, &event) == ObserveResult::Gap);
  assert(subscriptions.snapshot(second, invocation, inventory, kMaxDevices, &needed) ==
         ObserveResult::Ok && needed == 1);
  assert(subscriptions.poll(second, invocation, &event) == ObserveResult::Empty);
  assert(registry.remove(first));
  assert(subscriptions.poll(second, invocation, &event) == ObserveResult::Next &&
         event.kind == EventKind::CapabilityLost);
  assert(subscriptions.poll(second, invocation, &event) == ObserveResult::Next &&
         event.kind == EventKind::Removed &&
         std::strcmp(event.identity, "board.gnss") == 0);

  assert(owner.begin() == false);
  owner.requestStop();
  assert(subscriptions.poll(second, invocation, &event) == ObserveResult::Denied);
  assert(subscriptions.subscribe(owner, &replacement) == ObserveResult::Denied && !replacement);
  owner.end();
  assert(subscriptions.count() == 0);
  assert(!ExecutionContext::current());
  assert(nextOwner.begin() && nextOwner.id() != invocation);
  assert(subscriptions.poll(second, invocation, &event) == ObserveResult::Denied);
  assert(subscriptions.poll(second, nextOwner.id(), &event) == ObserveResult::Stale);
  SubscriptionHandle fresh = 0;
  assert(subscriptions.subscribe(nextOwner, &fresh) == ObserveResult::Ok && fresh != second);
  assert(subscriptions.unsubscribe(fresh, nextOwner.id()) == ObserveResult::Ok);
  assert(subscriptions.count() == 0);
  assert(subscriptions.subscribe(nextOwner, &fresh) == ObserveResult::Ok);
  nextOwner.end();
  assert(subscriptions.count() == 0);

  // Fixed capacity fails closed, and teardown releases EVERY subscription.
  ExecutionContext capacityContext;
  assert(capacityContext.begin());
  SubscriptionHandle handles[kMaxDeviceSubscriptions]{};
  for (size_t i = 0; i < kMaxDeviceSubscriptions; ++i)
    assert(subscriptions.subscribe(capacityContext, &handles[i]) == ObserveResult::Ok);
  assert(subscriptions.count() == kMaxDeviceSubscriptions);
  SubscriptionHandle denied = 77;
  assert(subscriptions.subscribe(capacityContext, &denied) == ObserveResult::Limit && denied == 0);
  capacityContext.end();
  assert(subscriptions.count() == 0);
  std::puts("Device event context-owned subscriptions and gap-resnapshot tests passed");
}
