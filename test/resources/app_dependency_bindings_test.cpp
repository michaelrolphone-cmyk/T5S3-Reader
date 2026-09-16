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

  AppCapabilityRequirements mandatory{};
  assert(addRequirement(&mandatory, "location.position", ">=1"));
  assert(addRequirement(&mandatory, "serial.port", ">=2"));
  size_t failed = 99;
  AppDependencyBindings bindings;
  assert(bindings.bind(registry, mandatory, 101, &failed));
  assert(failed == mandatory.count && bindings.count() == 2 && registry.leaseCount() == 2);
  assert(bindings.valid(registry, 101));
  assert(!bindings.valid(registry, 102));
  assert(!bindings.bind(registry, mandatory, 102));

  // Dependencies are observational, not access rights or physical reservations.
  LeaseHandle physical = 0;
  assert(registry.acquire("serial.port", 101, &physical, usbId, Mode::Exclusive) == Result::Ok);
  assert(registry.valid(physical, 101));
  LeaseInfo access{};
  assert(registry.getLease(physical, 101, &access) && access.mode == Mode::Exclusive);
  assert(bindings.valid(registry, 101));
  assert(registry.release(physical, 101) == Result::Ok);
  assert(bindings.valid(registry, 101));

  // Removal revokes the exact generation; replug cannot resurrect a lease.
  assert(registry.remove(usbId));
  assert(!bindings.valid(registry, 101));
  assert(registry.add(port, State::Available, &usbId));
  assert(!bindings.valid(registry, 101));
  assert(registry.leaseCount() == 1);
  bindings.release(registry, 102);
  assert(registry.leaseCount() == 1);
  bindings.release(registry, 101);
  bindings.release(registry, 101);
  assert(registry.leaseCount() == 0);

  // Partial failures release all preceding dependencies.
  assert(registry.remove(usbId));
  assert(!bindings.bind(registry, mandatory, 102, &failed));
  assert(failed == 1 && registry.leaseCount() == 0 && bindings.count() == 0);
  const uint16_t oldVersion[] = {1};
  const Descriptor outdated{"usb.session.2", "USB", "other.cdc", Transport::Usb,
                            portCaps, 1, 10, oldVersion};
  assert(registry.add(outdated, State::Available, &usbId));
  assert(resolveRequirement(registry, mandatory.entries[1]) == RequirementResult::ApiTooOld);
  assert(!bindings.bind(registry, mandatory, 102, &failed));
  assert(failed == 1 && registry.leaseCount() == 0);
  assert(registry.remove(usbId));
  assert(registry.add(port, State::Available, &usbId));

  // Exhaustion after the first claim must roll back that claim, not leak it.
  LeaseHandle filler[kMaxLeases]{};
  for (size_t i = 0; i < kMaxLeases - 1; ++i)
    assert(registry.acquire("location.position", 900, &filler[i], gpsId) == Result::Ok);
  assert(registry.leaseCount() == kMaxLeases - 1);
  assert(!bindings.bind(registry, mandatory, 103, &failed));
  assert(failed == 1 && bindings.count() == 0);
  assert(registry.leaseCount() == kMaxLeases - 1);
  assert(registry.releaseOwner(900) == kMaxLeases - 1);
  assert(bindings.bind(registry, mandatory, 103));
  assert(bindings.valid(registry, 103));
  bindings.release(registry, 103);
  assert(registry.leaseCount() == 0);
  std::puts("Atomic dependency bindings and generation/ownership tests passed");
}
