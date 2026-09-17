#include "runtime/capabilities/GnssStreamAuthority.h"
#include <cassert>
#include <cstdio>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

int main() {
  Registry devices;
  CapabilityAccess access(devices);
  GnssStreamAuthority guards(access, devices);
  constexpr const char* capabilities[] = {"location.position", "location.altitude"};
  const Descriptor desc{"board.gnss.uart0", "GNSS", "gps-nmea", Transport::Uart,
                        capabilities, 2, 100};
  DeviceHandle first = 0;
  assert(devices.add(desc, State::Available, &first));
  ExecutionContext context;
  assert(context.begin());
  const auto owner = context.id();
  constexpr t5_stream_t stream = 0x101u;
  constexpr uint64_t subscription = 0x100000001ull;
  LeaseHandle raw = 0, dependency = 0, consent = 0, altitude = 0;
  assert(devices.acquire("location.position", owner, &raw, first) == Result::Ok);
  assert(devices.acquire("location.position", owner, &dependency, first, Mode::Dependency) == Result::Ok);
  assert(!guards.bind(owner, first, raw, subscription, stream));
  assert(!guards.bind(owner, first, dependency, subscription, stream));
  assert(access.grantTrusted(context, first, "location.altitude", kCapabilityRead));
  assert(access.acquire(context, "location.altitude", first, kCapabilityRead, &altitude) == AccessResult::Ok);
  assert(!guards.bind(owner, first, altitude, subscription, stream));
  assert(access.grantTrusted(context, first, "location.position", kCapabilityRead));
  assert(access.acquire(context, "location.position", first, kCapabilityRead, &consent) == AccessResult::Ok);
  assert(guards.bind(owner, first, consent, subscription, stream));
  assert(guards.check(owner, stream) == GnssStreamAuthority::Check::Allowed);
  assert(guards.check(owner + 1, stream) == GnssStreamAuthority::Check::Denied);
  assert(guards.bound(owner, stream)); // Generic pipe delegation must be denied.
  assert(!guards.bind(owner, first, consent, subscription, stream));
  GnssStreamAuthority::Entry saved{};
  assert(guards.findStream(owner, stream, &saved) && saved.consent == consent &&
         saved.subscription == subscription && saved.device == first);
  assert(access.release(owner, consent) == AccessResult::Ok);
  assert(guards.check(owner, stream) == GnssStreamAuthority::Check::Denied);
  assert(access.acquire(context, "location.position", first, kCapabilityRead, &consent) == AccessResult::Ok);
  assert(guards.check(owner, stream) == GnssStreamAuthority::Check::Denied); // No implicit regrant.
  assert(guards.forgetSubscription(owner, subscription));
  assert(guards.check(owner, stream) == GnssStreamAuthority::Check::Unbound);
  assert(guards.bind(owner, first, consent, subscription + 1, stream + 1));
  assert(access.revokeTrusted(owner, first, "location.position"));
  assert(guards.check(owner, stream + 1) == GnssStreamAuthority::Check::Denied);
  assert(guards.forgetStream(owner, stream + 1));
  assert(access.grantTrusted(context, first, "location.position", kCapabilityRead));
  assert(access.acquire(context, "location.position", first, kCapabilityRead, &consent) == AccessResult::Ok);
  assert(guards.bind(owner, first, consent, subscription + 2, stream + 2));
  assert(devices.remove(first));
  DeviceHandle replacement = 0;
  assert(devices.add(desc, State::Available, &replacement) && replacement != first);
  assert(guards.check(owner, stream + 2) == GnssStreamAuthority::Check::Denied);
  guards.releaseOwner(owner);
  assert(guards.check(owner, stream + 2) == GnssStreamAuthority::Check::Unbound);
  context.end();
  std::puts("GNSS stream bindings reject revoked, replaced and forged authorization");
}
