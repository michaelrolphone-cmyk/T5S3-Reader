#include "runtime/capabilities/ProviderDevicePublisher.h"
#include "runtime/capabilities/UsbSerialProjection.h"
#include <cassert>
#include <cstring>
#include <cstdio>

int main() {
  using namespace RuntimeDevices;
  Registry registry;
  ProviderDevicePublisher publisher(registry);
  constexpr const char* caps[] = {"serial.port"};
  constexpr uint16_t versions[] = {1};
  const Descriptor first{"unit.serial.1", "Adapter", "installed.class.a",
                         Transport::Usb, caps, 1, 100, versions};
  assert(!publisher.publish(0, first));
  assert(publisher.publish(42, first));
  const DeviceHandle old = publisher.device();
  assert(old && registry.count() == 1);
  LeaseHandle lease = 0;
  assert(registry.acquire("serial.port", 7, &lease, old, Mode::Exclusive) == Result::Ok);
  assert(lease && registry.valid(lease, 7));
  assert(publisher.publish(42, first) && publisher.device() == old);
  assert(registry.valid(lease, 7));
  const Descriptor changed{"unit.serial.1", "Other adapter", "installed.class.a",
                           Transport::Usb, caps, 1, 100, versions};
  assert(!publisher.publish(42, changed));
  assert(publisher.device() == old && registry.valid(lease, 7));
  assert(publisher.publish(43, changed));
  const DeviceHandle next = publisher.device();
  assert(next && next != old && !registry.valid(lease, 7));
  assert(!publisher.publish(42, first));
  assert(publisher.device() == next);
  assert(publisher.withdraw() && registry.count() == 0);
  assert(!publisher.publish(43, changed));
  assert(publisher.publish(44, changed) && publisher.device() != next);
  assert(publisher.withdraw());

  UsbSerialProjection compatibility(registry);
  assert(compatibility.reconcile(0, 3, true, 0x10c4, 0xea60, 0, "Adapter"));
  const DeviceHandle legacy = compatibility.device();
  assert(legacy && compatibility.legacyId() == 3);
  assert(compatibility.reconcile(0, 3, true, 0x10c4, 0xea60, 0, "Adapter"));
  assert(compatibility.device() == legacy);
  assert(compatibility.clear() && compatibility.device() == 0);
  assert(!compatibility.reconcile(0, 3, true, 0x10c4, 0xea60, 0, "Adapter"));
  assert(compatibility.reconcile(1, 5, true, 0x10c4, 0xea60, 0, "Adapter"));
  assert(compatibility.device() != legacy);
  assert(compatibility.clear());
  std::puts("Provider semantic publication and revocation tests passed");
}
