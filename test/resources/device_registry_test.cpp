#include "runtime/capabilities/DeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace RuntimeDevices;
using RuntimeResources::ExecutionContext;

static void cleanup(void* opaque, uint32_t id) {
  auto& registry = *static_cast<Registry*>(opaque);
  assert(registry.releaseOwner(id) == 1);
}

int main() {
  Registry registry;
  const char* gnssCaps[] = {"location.position", "location.time"};
  const char* bleCaps[] = {"location.position", "sensor.temperature"};
  DeviceHandle uart = 0, ble = 0, replacement = 0;
  Descriptor gnss{"gnss-0", "GNSS", "gps-nmea", Transport::Uart, gnssCaps, 2, 50};
  Descriptor sensor{"ble-1", "Sensor", "ble-provider", Transport::Ble, bleCaps, 2, 20};
  assert(registry.add(gnss, State::Available, &uart) && uart);
  assert(registry.add(sensor, State::Available, &ble) && ble && ble != uart);
  assert(registry.count() == 2);
  DeviceInfo info{};
  assert(registry.get(uart, &info) && info.handle == uart &&
         std::strcmp(info.identity, "gnss-0") == 0 && info.capabilityCount == 2);
  assert(registry.at((uart & 0xffu) - 1u, &info));
  assert(!registry.add(gnss, State::Available, &replacement));
  assert(!replacement);
  const char* duplicated[] = {"sensor.temperature", "sensor.temperature"};
  Descriptor bad{"bad", "Bad", "provider", Transport::I2c, duplicated, 2, 0};
  assert(!registry.add(bad, State::Available, &replacement));
  Descriptor empty = gnss;
  empty.identity = "";
  assert(!registry.add(empty, State::Available, &replacement));

  ExecutionContext context;
  ExecutionContext other;
  assert(context.begin());
  const uint32_t firstOwner = context.id();
  LeaseHandle preferred = 0, shared = 0, denied = 123;
  // Unconstrained resolution picks the highest-priority matching device.
  assert(registry.acquire("location.position", firstOwner, &preferred) == Result::Ok);
  LeaseInfo lease{};
  assert(registry.getLease(preferred, firstOwner, &lease) && lease.device == ble);
  assert(!registry.valid(preferred, firstOwner + 1));
  assert(registry.release(preferred, firstOwner + 1) == Result::Stale);
  assert(registry.acquire("location.position", firstOwner, &denied, ble,
                          Mode::Exclusive) == Result::Busy && !denied);
  assert(registry.acquire("location.position", firstOwner, &shared, uart) == Result::Ok);
  assert(registry.valid(shared, firstOwner));
  assert(registry.release(preferred, firstOwner) == Result::Ok);
  assert(!registry.valid(preferred, firstOwner));
  assert(registry.acquire("sensor.temperature", firstOwner, &denied, ble,
                          Mode::Exclusive) == Result::Ok);
  assert(registry.acquire("location.position", firstOwner, &preferred, ble) == Result::Busy);
  assert(registry.release(denied, firstOwner) == Result::Ok);
  assert(registry.acquire("unknown", firstOwner, &preferred) == Result::NotFound);
  assert(registry.acquire("location.position", 0, &preferred) == Result::Invalid);
  assert(registry.acquire("location.position", firstOwner, &preferred, 0xdeadbeefu) ==
         Result::NotFound);
  assert(registry.leaseCount() == 1);

  // An unrelated device can disappear without revoking the GNSS session.
  assert(registry.setState(ble, State::Unavailable));
  assert(registry.valid(shared, firstOwner));
  assert(registry.acquire("location.position", firstOwner, &preferred, ble) ==
         Result::Unavailable);
  assert(registry.setState(ble, State::Available));
  assert(registry.acquire("location.position", firstOwner, &preferred, ble) == Result::Ok);
  assert(registry.setState(ble, State::Failed));
  assert(!registry.valid(preferred, firstOwner));
  assert(registry.release(preferred, firstOwner) == Result::Stale);
  assert(registry.setState(ble, State::Available));
  assert(registry.acquire("location.position", firstOwner, &denied, ble) == Result::Ok);
  assert(denied != preferred);  // Generation cannot alias a revoked lease.
  assert(registry.release(denied, firstOwner) == Result::Ok);

  // The provider's firmware-only cleanup runs before application ELF unload.
  assert(context.track(ExecutionContext::Resource::DeviceLeases, cleanup, &registry));
  context.end();
  assert(registry.leaseCount() == 0 && !registry.valid(shared, firstOwner));
  assert(!ExecutionContext::current());
  assert(registry.release(shared, firstOwner) == Result::Stale);

  // Reused device slot never resolves the previous device or a previous lease.
  assert(registry.remove(uart));
  assert(!registry.get(uart, &info));
  assert(registry.add(gnss, State::Available, &replacement));
  assert(replacement != uart && !registry.get(uart, &info));
  assert(other.begin());
  LeaseHandle fresh = 0;
  assert(registry.acquire("location.time", other.id(), &fresh, replacement) == Result::Ok);
  assert(!registry.valid(fresh, firstOwner));
  assert(registry.valid(fresh, other.id()));
  assert(registry.releaseOwner(firstOwner) == 0);
  assert(registry.releaseOwner(other.id()) == 1);
  other.end();
  assert(registry.remove(replacement) && registry.remove(ble));
  assert(registry.count() == 0 && registry.leaseCount() == 0);
  std::puts("Unified device registry and execution-context lease tests passed");
}
