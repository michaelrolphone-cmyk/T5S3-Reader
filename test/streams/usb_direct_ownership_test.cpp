// Reuse the production stream bridge fixture, then exercise direct USB
// acquisition specifically against the shared physical capability registry.
#define main run_existing_bridge_fixture
#include "bridge_test.cpp"
#undef main
#include "runtime/capabilities/DeviceRegistry.h"

int main() {
  assert(run_existing_bridge_fixture() == 0);
  unloadInRequest = false;
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

  t5_stream_t direct = 0, duplicate = 0;
  assert(streams->open_usb(&direct) == T5_STREAM_OK && direct);
  assert(devices.count() == 1 && devices.leaseCount() == 1);
  assert(streams->open_usb(&duplicate) == T5_STREAM_BUSY && !duplicate);
  // A failed duplicate open must not release the FIRST stream's ownership.
  assert(devices.count() == 1 && devices.leaseCount() == 1);
  t5_serial_port_request_t request{};
  request.config = {115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t port = 0;
  t5_stream_t rx = 0, tx = 0;
  assert(serial->acquire(&request, &port, &rx, &tx) == T5_SERIAL_BUSY);
  uint32_t n = 0;
  assert(streams->write(direct, "abc", 3, &n) == T5_STREAM_OK && n == 2);

  nativeUsbProviderDetach();
  nativeUsbProviderAttach(&usbStatus, 2);
  assert(streams->write(direct, "abc", 3, &n) == T5_STREAM_DISCONNECTED && n == 0);
  assert(devices.leaseCount() == 0);
  assert(streams->close(direct) == T5_STREAM_OK);
  assert(devices.leaseCount() == 0);
  nativeStreamsEnd();
  assert(devices.count() == 0 && devices.leaseCount() == 0);
  std::puts("Direct USB physical ownership and stale-stream tests passed");
}
