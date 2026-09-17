#include "runtime/capabilities/ProviderAuthorization.h"
#include <cassert>
#include <cstdio>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

int main() {
  Registry registry;
  CapabilityAccess access(registry);
  constexpr const char* capabilities[] = {"serial.port", "location.position"};
  const Descriptor descriptor{"usb.vidpid.interface", "USB serial", "usb.serial",
                              Transport::Usb, capabilities, 2, 100};
  DeviceHandle first = 0;
  assert(registry.add(descriptor, State::Available, &first));
  ExecutionContext context;
  assert(context.begin());
  const uint32_t owner = context.id();
  LeaseHandle permission = 0, physical = 0, dependency = 0;
  // Neither a dependency nor a hardware reservation grants application rights.
  assert(registry.acquire("serial.port", owner, &dependency, first, Mode::Dependency) == Result::Ok);
  assert(registry.acquire("serial.port", owner, &physical, first, Mode::Exclusive) == Result::Ok);
  assert(!ProviderAuthorization::semantic(access, registry, context, dependency, first,
                                          "serial.port", kCapabilityRead));
  assert(!ProviderAuthorization::io(access, registry, context, physical, physical, first,
                                    "serial.port", kCapabilityRead));
  assert(!ProviderAuthorization::io(access, registry, context, dependency, dependency, first,
                                    "serial.port", kCapabilityRead));
  assert(access.grantTrusted(context, first, "serial.port", kCapabilityRead));
  assert(access.acquire(context, "serial.port", first, kCapabilityRead, &permission) == AccessResult::Ok);
  assert(ProviderAuthorization::semantic(access, registry, context, permission, first,
                                         "serial.port", kCapabilityRead));
  assert(ProviderAuthorization::io(access, registry, context, permission, physical, first,
                                   "serial.port", kCapabilityRead));
  assert(!ProviderAuthorization::io(access, registry, context, permission, dependency, first,
                                    "serial.port", kCapabilityRead));
  assert(!ProviderAuthorization::io(access, registry, context, permission, physical, first,
                                    "serial.port", kCapabilityRead, Mode::Shared));
  assert(!ProviderAuthorization::semantic(access, registry, context, permission, first,
                                          "location.position", kCapabilityRead));
  assert(!ProviderAuthorization::semantic(access, registry, context, permission, first,
                                          "serial.port", kCapabilityWrite));
  assert(!ProviderAuthorization::io(access, registry, context, permission, physical, first,
                                    "serial.port", kCapabilityConfigure));
  assert(!ProviderAuthorization::semantic(access, registry, context, permission, 0,
                                          "serial.port", kCapabilityRead));
  assert(!ProviderAuthorization::semantic(access, registry, context, permission, first,
                                          nullptr, kCapabilityRead));
  assert(!ProviderAuthorization::semantic(access, registry, context, permission, first,
                                          "serial.port", 0));
  assert(!ProviderAuthorization::semantic(access, registry, context, permission, first,
                                          "serial.port", 8));
  // A physical lease can go stale independently of consent.
  assert(registry.release(physical, owner) == Result::Ok);
  assert(ProviderAuthorization::semantic(access, registry, context, permission, first,
                                         "serial.port", kCapabilityRead));
  assert(!ProviderAuthorization::io(access, registry, context, permission, physical, first,
                                    "serial.port", kCapabilityRead));
  assert(registry.acquire("serial.port", owner, &physical, first, Mode::Exclusive) == Result::Ok);
  // Releasing the issued authorization must immediately deny provider I/O.
  const LeaseHandle stalePermission = permission;
  assert(access.release(owner, permission) == AccessResult::Ok);
  assert(!ProviderAuthorization::io(access, registry, context, stalePermission, physical, first,
                                    "serial.port", kCapabilityRead));
  assert(access.acquire(context, "serial.port", first, kCapabilityRead, &permission) == AccessResult::Ok);
  assert(ProviderAuthorization::io(access, registry, context, permission, physical, first,
                                   "serial.port", kCapabilityRead));
  // Revocation invalidates already issued permissions without releasing hardware.
  assert(access.revokeTrusted(owner, first, "serial.port"));
  assert(!ProviderAuthorization::io(access, registry, context, permission, physical, first,
                                    "serial.port", kCapabilityRead));
  assert(registry.valid(physical, owner));
  assert(registry.remove(first));
  DeviceHandle replacement = 0;
  assert(registry.add(descriptor, State::Available, &replacement) && replacement != first);
  assert(!ProviderAuthorization::io(access, registry, context, permission, physical, replacement,
                                    "serial.port", kCapabilityRead));
  assert(access.grantTrusted(context, replacement, "serial.port", kCapabilityRead));
  assert(access.acquire(context, "serial.port", replacement, kCapabilityRead, &permission) == AccessResult::Ok);
  assert(registry.acquire("serial.port", owner, &physical, replacement, Mode::Exclusive) == Result::Ok);
  assert(ProviderAuthorization::io(access, registry, context, permission, physical, replacement,
                                   "serial.port", kCapabilityRead));
  context.requestStop();
  assert(!ProviderAuthorization::io(access, registry, context, permission, physical, replacement,
                                    "serial.port", kCapabilityRead));
  context.end();
  // Permission cleanup is independent: the provider still owns the physical
  // claim and must explicitly release it during its own shutdown.
  assert(access.owner() == 0 && registry.leaseCount() == 1);
  assert(registry.release(physical, owner) == Result::Ok);
  assert(registry.leaseCount() == 0);
  assert(context.begin() && context.id() != owner);
  assert(!ProviderAuthorization::io(access, registry, context, permission, physical, replacement,
                                    "serial.port", kCapabilityRead));
  context.end();
  std::puts("Provider authorization: exact capability/rights/device and physical lease validated");
}
