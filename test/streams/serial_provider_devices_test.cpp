#include "runtime/capabilities/SerialProviderDevices.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
risc_serial_device_v1 records[RISC_SERIAL_INVENTORY_MAX_DEVICES]{};
size_t present = 0;
bool uncertain = false;
bool partial = false;
uint32_t snapshots = 0;
uint64_t open(uint64_t device) { return device; }
bool configure(uint64_t, uint32_t, uint8_t, uint8_t, uint8_t) { return true; }
bool control(uint64_t, bool, bool) { return true; }
int32_t read(uint64_t, uint8_t*, size_t, uint32_t) { return 0; }
int32_t write(uint64_t, const uint8_t*, size_t, uint32_t) { return 0; }
bool close(uint64_t) { return true; }
int32_t probe(uint64_t) { return 1; }
bool snapshot(risc_serial_device_v1* out, size_t* capacity) {
  ++snapshots;
  if (!capacity || uncertain) return false;
  if (*capacity < present || (present && !out)) {
    *capacity = present;
    return false;
  }
  const size_t n = partial && present ? present - 1 : present;
  for (size_t i = 0; i < n; ++i) out[i] = records[i];
  *capacity = partial ? n : present;
  return true;
}
const risc_serial_port_inventory_v1 api = {
    {{RISC_SERIAL_PORT_API_V1, sizeof(risc_serial_port_inventory_v1),
      open, configure, control, read, write, close}, probe}, snapshot};
void announce(size_t i, uint64_t generation, uint8_t transport = RISC_SERIAL_TRANSPORT_USB) {
  records[i] = {};
  records[i].provider_device = generation;
  records[i].generation = generation;
  records[i].transport = transport;
}
}
int main() {
  using namespace RuntimeDevices;
  Registry registry;
  SerialProviderDevices devices(registry);
  assert(devices.refresh("usb-cp210x-v2", nullptr) == SerialProviderDevices::Result::Invalid);
  announce(0, 9);
  announce(1, 10);
  present = 2;
  assert(devices.refresh("usb-cp210x-v2", &api.discovery.serial) ==
         SerialProviderDevices::Result::Updated);
  assert(devices.count() == 2 && registry.count() == 2 && snapshots == 1);
  const DeviceHandle first = devices.deviceFor(9, 9);
  const DeviceHandle second = devices.deviceFor(10, 10);
  assert(first && second && first != second);
  uint64_t token = 0, generation = 0;
  assert(devices.resolve(first, &token, &generation) && token == 9 && generation == 9);
  assert(devices.resolve(second, &token, &generation) && token == 10 && generation == 10);
  assert(!devices.resolve(0, &token, &generation) && !token && !generation);
  DeviceInfo info{};
  assert(registry.get(first, &info) && info.transport == Transport::Usb &&
         !std::strcmp(info.provider, "usb-cp210x-v2") &&
         !std::strcmp(info.capabilities[0], "serial.port") &&
         info.capabilityApiVersions[0] == 1);
  const uint64_t unchangedCursor = registry.cursor();
  assert(devices.refresh("usb-cp210x-v2", &api.discovery.serial) ==
         SerialProviderDevices::Result::Updated);
  assert(registry.cursor() == unchangedCursor && devices.deviceFor(9, 9) == first);
  LeaseHandle lease = 0;
  assert(registry.acquire("serial.port", 7, &lease, first, Mode::Exclusive) ==
         RuntimeDevices::Result::Ok && registry.valid(lease, 7));

  // Inaccessible host/descriptors are UNKNOWN, not a disconnect or a
  // successful empty inventory. Preserve every handle and lease.
  uncertain = true;
  present = 0;
  assert(devices.refresh("usb-cp210x-v2", &api.discovery.serial) ==
         SerialProviderDevices::Result::Uncertain);
  assert(registry.valid(lease, 7) && registry.count() == 2 &&
         registry.cursor() == unchangedCursor);
  assert(devices.resolve(first, &token, &generation) && token == 9 && generation == 9);
  uncertain = false;
  present = 2;
  records[1].provider_device = records[0].provider_device;
  assert(devices.refresh("usb-cp210x-v2", &api.discovery.serial) ==
         SerialProviderDevices::Result::Invalid && registry.valid(lease, 7));
  announce(1, 10);
  records[0].reserved[0] = 1;
  assert(devices.refresh("usb-cp210x-v2", &api.discovery.serial) ==
         SerialProviderDevices::Result::Invalid);
  records[0].reserved[0] = 0;
  // Provider cannot secretly recharacterize a device in the same generation.
  records[0].transport = RISC_SERIAL_TRANSPORT_UART;
  assert(devices.refresh("usb-cp210x-v2", &api.discovery.serial) ==
         SerialProviderDevices::Result::RegistryFault);
  records[0].transport = RISC_SERIAL_TRANSPORT_USB;
  assert(registry.get(first, &info) && info.transport == Transport::Usb &&
         registry.valid(lease, 7));

  // Only a complete successful snapshot may announce detach.
  present = 1;
  records[0] = records[1];
  assert(devices.refresh("usb-cp210x-v2", &api.discovery.serial) ==
         SerialProviderDevices::Result::Updated);
  assert(!registry.get(first, &info) && !registry.valid(lease, 7) &&
         registry.get(second, &info) && devices.count() == 1);
  assert(!devices.resolve(first, &token, &generation) && !token && !generation);
  // Physical reconnect with identical provider identity creates a NEW handle.
  announce(1, 11);
  present = 2;
  assert(devices.refresh("usb-cp210x-v2", &api.discovery.serial) ==
         SerialProviderDevices::Result::Updated);
  const DeviceHandle replacement = devices.deviceFor(11, 11);
  assert(replacement && replacement != first && registry.count() == 2);
  assert(devices.resolve(replacement, &token, &generation) &&
         token == 11 && generation == 11);
  assert(!devices.resolve(first, &token, &generation) && !token && !generation);
  assert(devices.withdrawAll() && !devices.count() && !registry.count());
  assert(!devices.deviceFor(11, 11) && !registry.valid(lease, 7));
  assert(!devices.resolve(replacement, &token, &generation));

  // A separate packaged provider can independently publish the same opaque
  // physical token: identity contains the provider ID and is never an app
  // handle copied from the transport.
  SerialProviderDevices other(registry);
  announce(0, 11);
  present = 1;
  assert(other.refresh("usb-new-class", &api.discovery.serial) ==
         SerialProviderDevices::Result::Updated);
  assert(registry.count() == 1 && other.deviceFor(11, 11));
  assert(other.withdrawAll() && registry.count() == 0);
  std::puts("Provider-originated serial inventory, failure, detach and reconnect: PASS");
}
