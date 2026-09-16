#include "runtime/capabilities/AppDependencyBindings.h"
#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

struct Fixture {
  Registry* registry;
  AppDependencyBindings* bindings;
  unsigned cleanups = 0;
};

static void cleanup(void* opaque, uint32_t invocation) {
  auto* fixture = static_cast<Fixture*>(opaque);
  ++fixture->cleanups;
  fixture->bindings->release(*fixture->registry, invocation);
}

int main() {
  Registry registry;
  AppDependencyBindings bindings;
  Fixture fixture{&registry, &bindings};
  AppCapabilityRequirements requirements{};
  assert(addRequirement(&requirements, "sensor.temperature", ">=1"));
  const char* names[] = {"sensor.temperature"};
  const uint16_t versions[] = {1};
  const Descriptor descriptor{"sensor.built.in", "Temperature", "thermo", Transport::I2c,
                              names, 1, 100, versions};
  DeviceHandle sensor = 0;
  assert(registry.add(descriptor, State::Available, &sensor));

  ExecutionContext first;
  assert(first.begin());
  const uint32_t firstId = first.id();
  assert(bindings.bind(registry, requirements, firstId));
  assert(first.track(ExecutionContext::Resource::Dependencies, cleanup, &fixture));
  assert(registry.leaseCount() == 1 && bindings.valid(registry, firstId));
  first.requestStop();
  assert(!first.running(firstId));
  first.end();
  first.end();
  assert(fixture.cleanups == 1 && registry.leaseCount() == 0 && bindings.owner() == 0);
  assert(ExecutionContext::current() == nullptr);

  ExecutionContext second;
  assert(second.begin());
  const uint32_t secondId = second.id();
  assert(secondId != firstId);
  assert(bindings.bind(registry, requirements, secondId));
  assert(second.track(ExecutionContext::Resource::Dependencies, cleanup, &fixture));
  assert(!bindings.valid(registry, firstId));
  assert(registry.remove(sensor));  // Host detach revokes the active claim.
  assert(!bindings.valid(registry, secondId));
  second.end();
  assert(fixture.cleanups == 2 && registry.leaseCount() == 0);
  assert(registry.add(descriptor, State::Available, &sensor));
  assert(ExecutionContext::current() == nullptr);

  ExecutionContext third;
  assert(third.begin());
  const uint32_t thirdId = third.id();
  assert(bindings.bind(registry, requirements, thirdId));
  assert(third.track(ExecutionContext::Resource::Dependencies, cleanup, &fixture));
  // A normally returning ELF explicitly releases before unload and untracks.
  bindings.release(registry, thirdId);
  assert(third.untrack(ExecutionContext::Resource::Dependencies, thirdId));
  third.end();
  assert(fixture.cleanups == 2 && registry.leaseCount() == 0);
  assert(ExecutionContext::current() == nullptr);
  std::puts("Dependency execution-context lifecycle and abort cleanup tests passed");
}
