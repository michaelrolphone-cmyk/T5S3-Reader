#include "runtime/capabilities/ProviderAuthorization.h"
#include <cassert>
#include <cstdio>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

int main() {
  Registry registry;
  CapabilityAccess policy(registry);
  constexpr const char* caps[] = {"serial.port"};
  const Descriptor descriptor{"usb.cdc.1", "Serial", "usb.serial", Transport::Usb, caps, 1};
  DeviceHandle device = 0;
  assert(registry.add(descriptor, State::Available, &device));
  ExecutionContext context;
  assert(context.begin());
  const uint32_t owner = context.id();
  const uint32_t all = kCapabilityRead | kCapabilityWrite | kCapabilityConfigure;
  assert(policy.grantTrusted(context, device, "serial.port", all));
  LeaseHandle authorization = 0, physical = 0;
  assert(policy.acquire(context, "serial.port", device, all, &authorization) == AccessResult::Ok);
  assert(registry.acquire("serial.port", owner, &physical, device, Mode::Shared) == Result::Ok);
  assert(ProviderAuthorization::io(policy, registry, context, authorization, physical, device,
                                   "serial.port", kCapabilityRead, Mode::Shared));
  assert(!ProviderAuthorization::io(policy, registry, context, authorization, physical, device,
                                    "serial.port", kCapabilityWrite, Mode::Shared));
  assert(!ProviderAuthorization::io(policy, registry, context, authorization, physical, device,
                                    "serial.port", kCapabilityConfigure, Mode::Shared));
  assert(registry.release(physical, owner) == Result::Ok);
  assert(registry.acquire("serial.port", owner, &physical, device, Mode::Exclusive) == Result::Ok);
  assert(ProviderAuthorization::io(policy, registry, context, authorization, physical, device,
                                   "serial.port", kCapabilityWrite));
  assert(ProviderAuthorization::io(policy, registry, context, authorization, physical, device,
                                   "serial.port", kCapabilityConfigure));
  assert(policy.revokeTrusted(owner, device, "serial.port"));
  assert(!ProviderAuthorization::io(policy, registry, context, authorization, physical, device,
                                    "serial.port", kCapabilityWrite));
  assert(registry.release(physical, owner) == Result::Ok);
  context.end();
  assert(registry.leaseCount() == 0);
  std::puts("Provider mode/rights: shared reads and exclusive writes/configure passed");
}
