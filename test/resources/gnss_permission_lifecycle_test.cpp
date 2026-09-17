#include "runtime/capabilities/CapabilityAccess.h"
#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>

using RuntimeResources::ExecutionContext;
using namespace RuntimeDevices;

struct Fixture {
  Registry* registry = nullptr;
  ExecutionContext* context = nullptr;
  LeaseHandle physical = 0;
  unsigned gpsCleanups = 0;
  unsigned streamCleanups = 0;
};

static void gpsCleanup(void* opaque, uint32_t owner) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.context->id() == owner);
  assert(f.context->state() == ExecutionContext::State::Stopping);
  assert(f.registry->valid(f.physical, owner));
  assert(f.registry->release(f.physical, owner) == Result::Ok);
  ++f.gpsCleanups;
}

static void streamCleanup(void* opaque, uint32_t owner) {
  auto& f = *static_cast<Fixture*>(opaque);
  assert(f.context->id() == owner);
  assert(f.registry->leaseCount() == 0);
  ++f.streamCleanups;
}

static void run(bool requestPermissionFirst) {
  Registry registry;
  CapabilityAccess access(registry);
  ExecutionContext context;
  Fixture fixture{&registry, &context};
  constexpr const char* capabilities[] = {"location.position"};
  const Descriptor descriptor{"board.gnss.uart0", "GNSS", "gps-nmea",
                              Transport::Uart, capabilities, 1, 100};
  DeviceHandle device = 0;
  assert(registry.add(descriptor, State::Available, &device));
  assert(context.begin());
  const uint32_t owner = context.id();
  assert(context.track(ExecutionContext::Resource::Streams, streamCleanup, &fixture));
  auto permission = [&]() {
    assert(access.grantTrusted(context, device, "location.position", kCapabilityRead));
    LeaseHandle lease = 0;
    assert(access.acquire(context, "location.position", device, kCapabilityRead, &lease) == AccessResult::Ok);
    assert(lease && access.valid(owner, lease, kCapabilityRead));
  };
  auto gps = [&]() {
    assert(registry.acquire("location.position", owner, &fixture.physical, device) == Result::Ok);
    assert(context.track(ExecutionContext::Resource::GnssDriver, gpsCleanup, &fixture));
  };
  if (requestPermissionFirst) { permission(); gps(); }
  else { gps(); permission(); }
  assert(registry.leaseCount() == 2);
  // Neither lifecycle handler can be registered twice by the same invocation.
  assert(!context.track(ExecutionContext::Resource::GnssDriver, gpsCleanup, &fixture));
  assert(!context.track(ExecutionContext::Resource::DeviceLeases, gpsCleanup, &fixture));
  context.end();
  assert(fixture.gpsCleanups == 1 && fixture.streamCleanups == 1);
  assert(registry.leaseCount() == 0 && access.owner() == 0);
  assert(ExecutionContext::current() == nullptr);
  assert(context.begin() && context.id() != owner);
  assert(!access.valid(context.id(), fixture.physical, kCapabilityRead));
  context.end();
}

int main() {
  run(true);  // New API v3 grants READ before starting the GPS driver.
  run(false); // An already-started legacy GPS driver may be authorized later.
  std::puts("GNSS driver and permission execution-context cleanup tests passed");
}
