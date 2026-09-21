// Production serial bridge with a simulated asynchronous USB host. Provider
// identity outlives a consumer; uncertain class or endpoint teardown does not.
#include <T5AppApi.h>
#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include "native/NativeSerialPortBridge.h"
#include "native/NativeUsbClassBridge.h"
#include "native/NativeStreamBridge.h"
#include "native/NativeUsbDeviceRegistry.h"
#include "runtime/capabilities/DeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>
#include <cstring>

void nativeUsbTestFailCheckedStop(bool fail); // test-only class fault injection
namespace {
t5_app_api_v1 app{};
t5_usb_api_v1 usb{};
t5_usb_serial_state_t state{};
bool streamBusy = false, rejectOpenPair = false, rejectPartialPair = false;
bool failRxClose = false, failTxClose = false;
unsigned starts = 0, stops = 0, closes = 0;
bool classStarted = false;
}
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  return version == T5_APP_ABI_VERSION ? &app : nullptr;
}
extern "C" const t5_usb_api_v1* t5_usb_get_api(uint32_t version) {
  return version == T5_USB_API_VERSION ? &usb : nullptr;
}
bool nativeUsbClassAvailable() { return true; }
bool nativeUsbClassEnsureInstalled(uint16_t) { return true; }
bool nativeUsbClassAttachPair(uint32_t, t5_stream_t, t5_stream_t) { return true; }
bool nativeUsbClassStart(const t5_serial_config_t&) {
  if (classStarted) return false;
  ++starts;
  classStarted = true;
  return true;
}
void nativeUsbClassStop() {
  if (!classStarted) return; // Checked stop is idempotent on a stopped class.
  ++stops;
  classStarted = false;
  // The host alone owns detach. Do not clear a still-published device.
}
bool nativeUsbClassConfigure(const t5_serial_config_t&) { return true; }
bool nativeUsbClassControl(bool, bool) { return true; }
bool nativeUsbClassReadState(t5_usb_serial_state_t* out) {
  if (!out) return false;
  *out = state;
  return true;
}
bool nativeStreamUsbIsBusy() { return streamBusy; }
t5_stream_result_t nativeStreamOpenUsbPair(t5_stream_t* rx, t5_stream_t* tx) {
  if (rejectOpenPair) return T5_STREAM_IO; // Fail after physical class open.
  if (!rx || !tx || streamBusy) return T5_STREAM_BUSY;
  *rx = 11; *tx = 12; streamBusy = true;
  return rejectPartialPair ? T5_STREAM_IO : T5_STREAM_OK;
}
t5_stream_result_t nativeStreamCloseOwned(t5_stream_t handle) {
  assert(handle == 11 || handle == 12);
  if ((handle == 11 && failRxClose) || (handle == 12 && failTxClose))
    return T5_STREAM_IO; // Handle remains owned and retryable.
  if (++closes % 2 == 0) streamBusy = false;
  return T5_STREAM_OK;
}

int main() {
  usb.supported = [] { return true; };
  usb.serial_start = [](const t5_usb_line_coding_t*) { ++starts; return true; };
  usb.serial_stop = [] { ++stops; nativeUsbProviderDetach(); streamBusy = false; };
  usb.serial_read_state = [](t5_usb_serial_state_t* out) { *out = state; return true; };
  usb.serial_set_line_coding = [](const t5_usb_line_coding_t*) { return true; };
  usb.serial_set_control_lines = [](bool, bool) { return true; };
  state.connected = 1;
  state.status = T5_USB_STATUS_READY;
  state.vid = 0x10C4; state.pid = 0xEA60;
  std::strcpy(state.product, "USB GPS adapter");

  RuntimeResources::ExecutionContext invocation;
  assert(invocation.begin());
  nativeSerialPortsBegin();
  const auto* serial = t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION);
  assert(serial);
  auto& devices = RuntimeDevices::systemRegistry();
  assert(devices.count() == 0);
  t5_serial_port_request_t request{};
  request.config = {115200, 8, T5_SERIAL_PARITY_NONE, 1, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t lease = 0;
  t5_stream_t rx = 0, tx = 0;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && starts == 1 && devices.count() == 0 && devices.leaseCount() == 0);

  nativeUsbProviderAttach(&state, 2);
  t5_serial_port_state_t status{};
  assert(serial->read_status(lease, &status) == T5_SERIAL_OK && status.connected);
  assert(devices.count() == 1 && devices.leaseCount() == 1);
  RuntimeDevices::DeviceInfo first{};
  bool found = false;
  for (size_t index = 0; index < RuntimeDevices::kMaxDevices; ++index) {
    if (devices.at(index, &first)) { found = true; break; }
  }
  assert(found && first.transport == RuntimeDevices::Transport::Usb);
  assert(std::strcmp(first.capabilities[0], "serial.port") == 0);
  RuntimeDevices::LeaseHandle conflict = 99;
  assert(devices.acquire("serial.port", invocation.id(), &conflict, first.handle,
                         RuntimeDevices::Mode::Exclusive) == RuntimeDevices::Result::Busy);
  assert(conflict == 0);

  nativeUsbProviderDetach();
  assert(serial->read_status(lease, &status) == T5_SERIAL_OK &&
         !status.connected && status.last_error == T5_SERIAL_DISCONNECTED);
  assert(devices.count() == 0 && devices.leaseCount() == 0);
  assert(!devices.get(first.handle, &first));

  nativeUsbProviderAttach(&state, 2);
  assert(serial->read_status(lease, &status) == T5_SERIAL_OK &&
         !status.connected && status.last_error == T5_SERIAL_DISCONNECTED);
  assert(devices.count() == 1 && devices.leaseCount() == 0);
  RuntimeDevices::DeviceInfo replacement{};
  found = false;
  for (size_t index = 0; index < RuntimeDevices::kMaxDevices; ++index) {
    if (devices.at(index, &replacement)) { found = true; break; }
  }
  assert(found && replacement.handle != first.handle);
  assert(serial->release(lease) == T5_SERIAL_OK);
  assert(stops == 1 && closes == 2 && devices.count() == 1 && devices.leaseCount() == 0);
  RuntimeDevices::DeviceInfo stillPresent{};
  assert(devices.get(replacement.handle, &stillPresent));

  // An open that acquired a class but failed to publish stream endpoints must
  // retain the exact private token when physical close is uncertain.
  rejectOpenPair = true;
  nativeUsbTestFailCheckedStop(true);
  lease = rx = tx = 91;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_IO);
  assert(!lease && !rx && !tx && starts == 2 && stops == 1);
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_BUSY);
  assert(!lease && !rx && !tx && starts == 2 && stops == 1);
  nativeUsbTestFailCheckedStop(false);
  rejectOpenPair = false;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && rx == 11 && tx == 12 && starts == 3 && stops == 2);

  // A physically quiesced class is insufficient: both endpoint handles must
  // also close before retiring the public lease and releasing physical rights.
  failTxClose = true;
  assert(serial->release(lease) == T5_SERIAL_IO);
  assert(stops == 3 && closes == 3 && devices.count() == 1);
  t5_serial_port_lease_t refused = 99;
  assert(serial->acquire(&request, &refused, &rx, &tx) == T5_SERIAL_BUSY && !refused);
  failTxClose = false;
  assert(serial->release(lease) == T5_SERIAL_OK && stops == 3 && closes == 4);
  assert(devices.count() == 1 && devices.leaseCount() == 0);

  // Even a failing open that returns partially allocated endpoints must not
  // leak their handles. A failing RX close is retried on the same private
  // generation before a new session can be acquired.
  rejectPartialPair = true;
  failRxClose = true;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_IO);
  assert(!lease && !rx && !tx && starts == 4 && stops == 4);
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_BUSY);
  assert(!lease && !rx && !tx && starts == 4 && stops == 4);
  failRxClose = false;
  rejectPartialPair = false;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && starts == 5 && closes == 6 && stops == 4);
  assert(serial->release(lease) == T5_SERIAL_OK);
  assert(stops == 5 && closes == 8 && devices.count() == 1 && devices.leaseCount() == 0);

  nativeSerialPortsEnd();
  assert(devices.count() == 1 && devices.get(replacement.handle, &stillPresent));
  invocation.end();
  assert(!RuntimeResources::ExecutionContext::current());
  nativeUsbProviderDetach();
  nativeDeviceDiscoveryTick();
  assert(devices.count() == 0 && devices.leaseCount() == 0);
  std::puts("USB serial partial-open, endpoint teardown retry and generation tests passed");
}
