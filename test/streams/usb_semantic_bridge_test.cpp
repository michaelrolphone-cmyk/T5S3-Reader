// Production serial bridge with a simulated asynchronous USB host. Verifies
// that legacy USB events become unified, execution-context-owned devices.
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

namespace {
t5_app_api_v1 app{};
t5_usb_api_v1 usb{};
t5_usb_serial_state_t state{};
bool streamBusy = false;
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
bool nativeUsbClassStart(const t5_serial_config_t&) {
  if (classStarted) return false;
  ++starts;
  classStarted = true;
  return true;
}
void nativeUsbClassStop() {
  ++stops;
  classStarted = false;
  streamBusy = false;
  nativeUsbProviderDetach();
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
  if (!rx || !tx || streamBusy) return T5_STREAM_BUSY;
  *rx = 11; *tx = 12; streamBusy = true;
  return T5_STREAM_OK;
}
t5_stream_result_t nativeStreamCloseOwned(t5_stream_t handle) {
  assert(handle == 11 || handle == 12);
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

  // Enumeration arrives after serial host startup, on the USB callback path.
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

  // Identical VID/PID/product cannot resurrect the previous lease.
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
  assert(stops == 1 && closes == 2 && devices.count() == 0 && devices.leaseCount() == 0);
  nativeSerialPortsEnd();
  invocation.end();
  assert(!RuntimeResources::ExecutionContext::current());
  std::puts("USB semantic device/owner lease bridge tests passed");
}
