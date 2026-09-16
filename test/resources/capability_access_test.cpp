#include "runtime/capabilities/CapabilityAccess.h"
#include <cassert>
#include <cstdio>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

int main() {
  Registry registry;
  CapabilityAccess access(registry);
  const char* caps[] = {"location.position", "serial.port"};
  const uint16_t versions[] = {1, 1};
  const Descriptor first{"device.1", "Device", "generic.provider", Transport::Usb,
                         caps, 2, 1, versions};
  DeviceHandle device = 0;
  assert(registry.add(first, State::Available, &device));
  ExecutionContext ctx;
  LeaseHandle lease = 999;
  assert(!access.grantTrusted(ctx, device, "location.position", kCapabilityRead));
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &lease) == AccessResult::Denied);
  assert(lease == 0);
  assert(ctx.begin());
  const uint32_t owner = ctx.id();
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &lease) == AccessResult::Denied);
  assert(!access.grantTrusted(ctx, device, "location.position", 0));
  assert(!access.grantTrusted(ctx, device, "location.position", 8));
  assert(!access.grantTrusted(ctx, device, "unknown.capability", kCapabilityRead));
  assert(!access.grantTrusted(ctx, 0x01000001u, "location.position", kCapabilityRead));
  assert(access.grantTrusted(ctx, device, "location.position", kCapabilityRead));
  assert(access.owner() == owner);
  assert(access.acquire(ctx, "location.position", device, kCapabilityWrite, &lease) == AccessResult::Denied);
  assert(access.acquire(ctx, "serial.port", device, kCapabilityRead, &lease) == AccessResult::Denied);
  assert(access.acquire(ctx, "location.position", 0, kCapabilityRead, &lease) == AccessResult::Invalid);
  assert(access.acquire(ctx, "location.position", device, 8, &lease) == AccessResult::Invalid);
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &lease) == AccessResult::Ok);
  assert(lease && access.valid(owner, lease, kCapabilityRead));
  assert(!access.valid(owner, lease, kCapabilityWrite));
  assert(!access.valid(owner + 1, lease, kCapabilityRead));
  assert(!access.valid(owner, lease, 0));
  DeviceHandle chosen = 999;
  assert(!access.valid(owner, lease, kCapabilityWrite, &chosen) && chosen == 0);
  assert(access.valid(owner, lease, kCapabilityRead, &chosen) && chosen == device);

  // Semantic authority does not block or substitute for physical sessions.
  LeaseHandle physical = 0;
  assert(registry.acquire("serial.port", owner, &physical, device, Mode::Exclusive) == Result::Ok);
  assert(registry.valid(physical, owner));
  assert(access.valid(owner, lease, kCapabilityRead));
  assert(registry.release(physical, owner) == Result::Ok);

  assert(access.grantTrusted(ctx, device, "location.position", kCapabilityWrite));
  assert(access.acquire(ctx, "location.position", device,
                        kCapabilityRead | kCapabilityWrite, &physical) == AccessResult::Ok);
  assert(access.valid(owner, physical, kCapabilityWrite));
  assert(!access.valid(owner, lease, kCapabilityWrite));  // Rights cannot be retroactively widened.
  assert(access.revokeTrusted(owner, device, "location.position"));
  assert(!access.valid(owner, lease, kCapabilityRead));
  assert(!access.valid(owner, physical, kCapabilityWrite));
  assert(access.release(owner, lease) == AccessResult::Stale);
  assert(registry.leaseCount() == 0);
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &lease) == AccessResult::Denied);

  assert(access.grantTrusted(ctx, device, "location.position", kCapabilityRead));
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &lease) == AccessResult::Ok);
  const LeaseHandle oldLease = lease;
  assert(registry.remove(device));
  assert(!access.valid(owner, lease, kCapabilityRead));
  assert(registry.add(first, State::Available, &device));
  assert(!access.valid(owner, oldLease, kCapabilityRead));
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &lease) == AccessResult::Denied);
  assert(access.release(owner, oldLease) == AccessResult::Ok);
  assert(access.grantTrusted(ctx, device, "location.position", kCapabilityRead));
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &lease) == AccessResult::Ok);
  assert(access.valid(owner, lease, kCapabilityRead));
  assert(access.release(owner + 1, lease) == AccessResult::Stale);
  assert(access.valid(owner, lease, kCapabilityRead));
  ctx.requestStop();
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &physical) == AccessResult::Denied);
  assert(!access.grantTrusted(ctx, device, "serial.port", kCapabilityRead));
  ctx.end();
  assert(registry.leaseCount() == 0);
  assert(access.owner() == 0);
  assert(!access.valid(owner, lease, kCapabilityRead));
  assert(ctx.begin());
  assert(ctx.id() != owner);
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &physical) == AccessResult::Denied);
  assert(access.grantTrusted(ctx, device, "location.position", kCapabilityRead));
  assert(access.acquire(ctx, "location.position", device, kCapabilityRead, &physical) == AccessResult::Ok);
  assert(physical != oldLease);
  ctx.end();
  assert(registry.leaseCount() == 0);
  std::puts("Capability grants, scoped rights and lifecycle tests passed");
}
