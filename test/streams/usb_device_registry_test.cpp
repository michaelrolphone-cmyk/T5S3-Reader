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
  registry.observe(state);
  auto a = registry.snapshot();
  assert(a.id && a.transport == Transport::Usb && a.provider == Provider::UsbSerial);
  assert(a.presence == Presence::Bound && a.serial_port);
  assert(a.vid == 0x1234 && a.pid == 0x5678 && a.interface_number == 0xff);
  assert(registry.resolve(a.id, Provider::UsbSerial));
  assert(!registry.resolve(a.id, Provider::None));
  state.status = T5_USB_STATUS_READY;
  registry.observe(state);
  assert(registry.snapshot().id == a.id); // Status/line-coding transitions do not change identity.

  // A physically replaced device invalidates its predecessor, even if its
  // descriptor identity is identical. The host publishes the WAITING state.
  state.connected = 0;
  state.status = T5_USB_STATUS_WAITING;
  registry.observe(state);
  assert(registry.snapshot().id == 0 && !registry.resolve(a.id, Provider::UsbSerial));
  state.connected = 1;
  state.status = T5_USB_STATUS_READY;
  registry.observe(state);
  auto b = registry.snapshot();
  assert(b.id && b.id != a.id && registry.resolve(b.id, Provider::UsbSerial));
  assert(!registry.resolve(a.id, Provider::UsbSerial));

  // Descriptor change must invalidate the old device even if an unplug status
  // was missed by the consumer's polling cadence.
  state.pid = 0x5679;
  registry.observe(state);
  auto c = registry.snapshot();
  assert(c.id && c.id != b.id && c.pid == 0x5679);
  assert(!registry.resolve(b.id, Provider::UsbSerial));

  state.status = T5_USB_STATUS_OFF;
  registry.observe(state);
  assert(!registry.snapshot().id && !registry.resolve(c.id, Provider::UsbSerial));
  registry.detach();
  assert(registry.snapshot().presence == Presence::Unavailable);
  std::cout << "USB runtime device registry tests passed\n";
}
