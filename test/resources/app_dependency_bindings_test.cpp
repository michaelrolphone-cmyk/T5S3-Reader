#include "runtime/capabilities/AppDependencyBindings.h"
#include <cassert>
#include <cstdio>

using namespace RuntimeDevices;

int main() {
  Registry registry;
  const char* gpsCaps[] = {"location.position"};
  const uint16_t gpsApis[] = {1};
  const Descriptor gps{"board.gps", "GPS", "provider.gnss", Transport::Uart,
                       gpsCaps, 1, 30, gpsApis};
  DeviceHandle gpsId = 0;
  assert(registry.add(gps, State::Available, &gpsId));
  const char* portCaps[] = {"serial.port"};
  const uint16_t portApis[] = {2};
  const Descriptor port{"usb.session.1", "USB", "thirdparty.cdc", Transport::Usb,
                        portCaps, 1, 10, portApis};
  DeviceHandle usbId = 0;
  assert(registry.add(port, State::Available, &usbId));

  AppCapabilityRequirements requires{};
  assert(addRequirement(&requires, "location.position", ">=1"));
  assert(addRequirement(&requires, "serial.port", ">=2"));
  size_t failed = 99;
  AppDependencyBindings bindings;
  assert(bindings.bind(registry, requires, 101, &failed));
  assert(failed == requires.count && bindings.count() == 2 && registry.leaseCount() == 2);
  assert(bindings.valid(registry, 101));
  assert(!bindings.valid(registry, 102));
  assert(!bindings.bind(registry, requires, 102));

  // Dependencies are observational, not access rights or physical reservations.
  // Both an exclusive USB owner and a separate shared GNSS consumer must work.
  LeaseHandle physical = 0;
  assert(registry.acquire("serial.port", 101, &physical, usbId, Mode::Exclusive) == Result::Ok);
  assert(registry.valid(physical, 101));
  LeaseInfo access{};
  assert(registry.getLease(physical, 101, &access) && access.mode == Mode::Exclusive);
  assert(bindings.valid(registry, 101));
  assert(registry.release(physical, 101) == Result::Ok);
  assert(bindings.valid(registry, 101));

  // Device removal revokes the exact generation. An identical replacement
  // cannot resurrect a stale dependency binding.
  assert(registry.remove(usbId));
  assert(!bindings.valid(registry, 101));
  assert(registry.add(port, State::Available, &usbId));
  assert(!bindings.valid(registry, 101));
  assert(registry.leaseCount() == 1);  // GNSS dependency still live.
  bindings.release(registry, 102);    // Wrong owner cannot discard it.
  assert(registry.leaseCount() == 1);
  bindings.release(registry, 101);
  bindings.release(registry, 101);
  assert(registry.leaseCount() == 0);

  // Partial failure must release all preceding dependencies. A missing second
  // provider and an API-floor mismatch leave no leaked resource handles.
  assert(registry.remove(usbId));
  assert(!bindings.bind(registry, requires, 102, &failed));
  assert(failed == 1 && registry.leaseCount() == 0 && bindings.count() == 0);
  const uint16_t oldVersion[] = {1};
  const Descriptor outdated{"usb.session.2", "USB", "other.cdc", Transport::Usb,
                            portCaps, 1, 10, oldVersion};
  assert(registry.add(outdated, State::Available, &usbId));
  assert(resolveRequirement(registry, requires.entries[1]) == RequirementResult::ApiTooOld);
  assert(!bindings.bind(registry, requires, 102, &failed));
  assert(failed == 1 && registry.leaseCount() == 0);
  assert(registry.remove(usbId));
  assert(registry.add(port, State::Available, &usbId));

  // A full lease table triggers rollback even when preflight saw both devices.
  LeaseHandle filler[kMaxLeases]{};
  for (size_t i = 0; i < kMaxLeases - 1; ++i)
    assert(registry.acquire("location.position", 900, &filler[i], gpsId) == Result::Ok);
  assert(registry.leaseCount() == kMaxLeases - 1);
  assert(!bindings.bind(registry, requires, 103, &failed));
  assert(failed == 1 && bindings.count() == 0);
  assert(registry.leaseCount() == kMaxLeases - 1);
  assert(registry.releaseOwner(900) == kMaxLeases - 1);
  assert(bindings.bind(registry, requires, 103));
  assert(bindings.valid(registry, 103));
  bindings.release(registry, 103);
  assert(registry.leaseCount() == 0);
  std::puts("Atomic dependency bindings and generation/ownership tests passed");
}
