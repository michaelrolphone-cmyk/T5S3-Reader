// Compile the real stream and serial bridges with the existing host fixture,
// but use an independent main to focus on direct USB physical ownership.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_existing_bridge_fixture
#include "bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "runtime/capabilities/DeviceRegistry.h"

int main() {
  usb.supported = [] { return true; };
  usb.serial_start = [](const t5_usb_line_coding_t* coding) {
    ++starts; ++configs; usbStatus.line_coding = *coding; return true;
  };
  usb.serial_stop = [] {
    ++stops; nativeUsbProviderDetach(); usbStatus.status = T5_USB_STATUS_OFF;
    usbStatus.connected = 0;
  };
  usb.serial_read_state = [](t5_usb_serial_state_t* out) { *out = usbStatus; return true; };
  usb.serial_set_line_coding = [](const t5_usb_line_coding_t* coding) {
    usbStatus.line_coding = *coding; return true;
  };
  usb.serial_set_control_lines = [](bool dtr, bool rts) {
    usbStatus.dtr = dtr; usbStatus.rts = rts; return true;
  };
  usb.serial_read = [](uint8_t*, size_t) -> size_t { return 0; };
  usb.serial_write = [](const uint8_t*, size_t n) -> size_t { return std::min<size_t>(2, n); };

  nativeStreamsBegin();
  const auto* streams = t5_stream_get_api(T5_STREAM_API_VERSION);
  const auto* serial = t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION);
  assert(streams && serial);
  auto& devices = RuntimeDevices::systemRegistry();
  usbStatus.status = T5_USB_STATUS_READY;
  usbStatus.connected = 1;
  usbStatus.vid = 0x1234;
  usbStatus.pid = 0x5678;
  std::strcpy(usbStatus.product, "USB direct regression");
  nativeUsbProviderAttach(&usbStatus, 2);

  t5_stream_t rejected = 0;
  assert(streams->open_usb(&rejected) == T5_STREAM_UNSUPPORTED && !rejected);
  assert(devices.leaseCount() == 0);

  t5_serial_port_request_t request{};
  request.config = {115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t port = 0, duplicate = 0;
  t5_stream_t rx = 0, tx = 0, busyRx = 0, busyTx = 0;
  assert(serial->acquire(&request, &port, &rx, &tx) == T5_SERIAL_OK && port && rx && tx);
  assert(devices.count() == 1 && devices.leaseCount() == 1);
  assert(serial->acquire(&request, &duplicate, &busyRx, &busyTx) == T5_SERIAL_BUSY);
  assert(devices.count() == 1 && devices.leaseCount() == 1);
  assert(streams->open_usb(&rejected) == T5_STREAM_UNSUPPORTED && !rejected);
  uint32_t n = 0;
  assert(streams->write(tx, "abc", 3, &n) == T5_STREAM_OK && n == 3);
  assert(streams->write(rx, "a", 1, &n) == T5_STREAM_DENIED);

  nativeUsbProviderDetach();
  nativeUsbProviderAttach(&usbStatus, 2);
  assert(streams->write(tx, "abc", 3, &n) == T5_STREAM_DISCONNECTED && n == 0);
  assert(serial->release(port) == T5_SERIAL_OK);
  assert(devices.leaseCount() == 0);
  nativeStreamsEnd();
  assert(devices.count() == 0 && devices.leaseCount() == 0);
  std::puts("serial.port exclusive ownership tests passed");
  return 0;
}
