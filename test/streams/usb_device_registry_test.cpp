#include "native/NativeUsbDeviceRegistry.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
  using namespace NativeUsbDevices;
  Registry registry;
  t5_usb_serial_state_t state{};
  state.status = T5_USB_STATUS_WAITING;
  registry.observe(state);
  assert(registry.snapshot().id == 0 && registry.epoch() == 0);
  assert(!registry.resolve(1, Provider::UsbSerial));

  state.connected = 1;
  state.status = T5_USB_STATUS_CONFIGURING;
  state.vid = 0x1234;
  state.pid = 0x5678;
  std::strcpy(state.product, "USB CDC A");
  registry.observe(state, 2); // USB host claimed real data interface 2.
  auto a = registry.snapshot();
  assert(a.id && a.transport == Transport::Usb && a.provider == Provider::UsbSerial);
  assert(a.presence == Presence::Bound && a.serial_port);
  assert(a.vid == 0x1234 && a.pid == 0x5678 && a.interface_number == 2);
  assert(registry.resolve(a.id, Provider::UsbSerial));
  assert(!registry.resolve(a.id, Provider::None));
  assert(registry.epoch() == 0); // Initial attach does not revoke a waiting lease.
  state.status = T5_USB_STATUS_READY;
  registry.observe(state, 2);
  assert(registry.snapshot().id == a.id && registry.epoch() == 0);

  // Simulate real host events without any read_status between them. Identical
  // VID/PID/product/interface still means a different physical device.
  registry.detach();
  assert(registry.epoch() == 1);
  assert(!registry.resolve(a.id, Provider::UsbSerial));
  registry.detach(); // A redundant teardown must not revoke the next lease.
  assert(registry.epoch() == 1);
  registry.observe(state, 2);
  auto b = registry.snapshot();
  assert(b.id && b.id != a.id && registry.resolve(b.id, Provider::UsbSerial));
  assert(!registry.resolve(a.id, Provider::UsbSerial));
  assert(registry.epoch() == 1);

  // An identity change without a DEV_GONE event also revokes old streams.
  registry.observe(state, 3);
  auto c = registry.snapshot();
  assert(c.id && c.id != b.id && c.interface_number == 3);
  assert(!registry.resolve(b.id, Provider::UsbSerial) && registry.epoch() == 2);

  state.pid = 0x5679;
  registry.observe(state, 3);
  auto d = registry.snapshot();
  assert(d.id && d.id != c.id && d.pid == 0x5679);
  assert(!registry.resolve(c.id, Provider::UsbSerial) && registry.epoch() == 3);

  state.connected = 0;
  state.status = T5_USB_STATUS_WAITING;
  registry.observe(state);
  assert(registry.snapshot().id == 0 && !registry.resolve(d.id, Provider::UsbSerial));
  assert(registry.epoch() == 4);
  state.connected = 1;
  state.status = T5_USB_STATUS_READY;
  registry.observe(state, 3);
  const auto e = registry.snapshot();
  assert(registry.epoch() == 4);
  state.status = T5_USB_STATUS_OFF;
  registry.observe(state);
  assert(!registry.snapshot().id && !registry.resolve(e.id, Provider::UsbSerial));
  assert(registry.epoch() == 5);
  registry.detach();
  assert(registry.snapshot().presence == Presence::Unavailable && registry.epoch() == 5);
  std::cout << "USB runtime device registry tests passed\n";
}
