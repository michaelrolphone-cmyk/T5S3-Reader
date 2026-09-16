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
  assert(registry.snapshot().id == 0);
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
  state.status = T5_USB_STATUS_READY;
  registry.observe(state, 2);
  assert(registry.snapshot().id == a.id);

  // Simulate real host events without calling observe/read_status between them.
  // Identical VID, PID, product and interface still represent a NEW device.
  registry.detach();
  assert(!registry.resolve(a.id, Provider::UsbSerial));
  registry.observe(state, 2);
  auto b = registry.snapshot();
  assert(b.id && b.id != a.id && registry.resolve(b.id, Provider::UsbSerial));
  assert(!registry.resolve(a.id, Provider::UsbSerial));

  // A data-interface change also invalidates a previous binding.
  registry.observe(state, 3);
  auto c = registry.snapshot();
  assert(c.id && c.id != b.id && c.interface_number == 3);
  assert(!registry.resolve(b.id, Provider::UsbSerial));

  // Descriptor change is a different bound device even without detach.
  state.pid = 0x5679;
  registry.observe(state, 3);
  auto d = registry.snapshot();
  assert(d.id && d.id != c.id && d.pid == 0x5679);
  assert(!registry.resolve(c.id, Provider::UsbSerial));

  state.connected = 0;
  state.status = T5_USB_STATUS_WAITING;
  registry.observe(state);
  assert(registry.snapshot().id == 0 && !registry.resolve(d.id, Provider::UsbSerial));
  state.connected = 1;
  state.status = T5_USB_STATUS_READY;
  registry.observe(state, 3);
  const auto e = registry.snapshot();
  state.status = T5_USB_STATUS_OFF;
  registry.observe(state);
  assert(!registry.snapshot().id && !registry.resolve(e.id, Provider::UsbSerial));
  registry.detach();
  assert(registry.snapshot().presence == Presence::Unavailable);
  std::cout << "USB runtime device registry tests passed\n";
}
