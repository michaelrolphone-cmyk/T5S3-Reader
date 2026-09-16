// Compile the production NativeSerialPortBridge, not just its resolver, with
// a USB stub and a second transport-independent semantic serial provider.
#include <T5AppApi.h>
#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include "native/NativeSerialPortBridge.h"
#include "native/NativeStreamBridge.h"
#include "native/NativeUsbDeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
t5_app_api_v1 app{};
t5_usb_api_v1 usb{};
t5_usb_serial_state_t usbStatus{};
bool appAllowed = true, usbSupported = true, usbStreamBusy = false;
int usbStarts = 0, usbStops = 0, usbCloses = 0;
constexpr t5_serial_device_t kAlternativeDevice = 0x80001001u;
struct Fake {
  bool online = true;
  int acquisitions = 0, releases = 0, configs = 0, controls = 0, statusReads = 0;
  t5_serial_config_t config{};
} fake;

bool available(void* ptr) { return static_cast<Fake*>(ptr)->online; }
bool matches(void*, t5_serial_device_t id) { return id == kAlternativeDevice; }
t5_serial_result_t acquire(void* ptr, const t5_serial_port_request_t* request,
                           t5_serial_port_lease_t* lease, t5_stream_t* rx, t5_stream_t* tx) {
  auto& f = *static_cast<Fake*>(ptr);
  assert(request && (request->device == 0 || request->device == kAlternativeDevice));
  ++f.acquisitions;
  f.config = request->config;
  *lease = 17; *rx = 41; *tx = 42;
  return T5_SERIAL_OK;
}
t5_serial_result_t configure(void* ptr, t5_serial_port_lease_t lease,
                             const t5_serial_config_t* config) {
  auto& f = *static_cast<Fake*>(ptr);
  assert(lease == 17 && config);
  ++f.configs;
  f.config = *config;
  return T5_SERIAL_OK;
}
t5_serial_result_t status(void* ptr, t5_serial_port_lease_t lease,
                          t5_serial_port_state_t* out) {
  auto& f = *static_cast<Fake*>(ptr);
  assert(lease == 17 && out);
  ++f.statusReads;
  *out = {};
  out->device = kAlternativeDevice;
  out->status = f.online ? T5_SERIAL_STATUS_READY : T5_SERIAL_STATUS_WAITING;
  out->connected = f.online;
  out->config = f.config;
  std::strcpy(out->device_label, "Alternative UART");
  return T5_SERIAL_OK;
}
t5_serial_result_t control(void* ptr, t5_serial_port_lease_t lease, bool, bool) {
  auto& f = *static_cast<Fake*>(ptr);
  assert(lease == 17);
  ++f.controls;
  return T5_SERIAL_OK;
}
t5_serial_result_t release(void* ptr, t5_serial_port_lease_t lease) {
  auto& f = *static_cast<Fake*>(ptr);
  assert(lease == 17);
  ++f.releases;
  return T5_SERIAL_OK;
}
RuntimeSerial::Provider alternative() {
  return {"alternate.serial", 10, &fake, available, matches, acquire,
          configure, status, control, release};
}
} // namespace

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  return appAllowed && version == T5_APP_ABI_VERSION ? &app : nullptr;
}
extern "C" const t5_usb_api_v1* t5_usb_get_api(uint32_t version) {
  return version == T5_USB_API_VERSION ? &usb : nullptr;
}
bool nativeStreamUsbIsBusy() { return usbStreamBusy; }
t5_stream_result_t nativeStreamOpenUsbPair(t5_stream_t* rx, t5_stream_t* tx) {
  assert(rx && tx && !usbStreamBusy);
  *rx = 20; *tx = 30;
  usbStreamBusy = true;
  return T5_STREAM_OK;
}
t5_stream_result_t nativeStreamCloseOwned(t5_stream_t handle) {
  assert(handle == 20 || handle == 30);
  if (++usbCloses % 2 == 0) usbStreamBusy = false;
  return T5_STREAM_OK;
}

int main() {
  usb.supported = [] { return usbSupported; };
  usb.serial_start = [](const t5_usb_line_coding_t* coding) {
    assert(coding && !usbStreamBusy);
    ++usbStarts;
    usbStatus.line_coding = *coding;
    return true;
  };
  usb.serial_stop = [] { ++usbStops; usbStreamBusy = false; nativeUsbProviderDetach(); };
  usb.serial_read_state = [](t5_usb_serial_state_t* out) { *out = usbStatus; return true; };
  usb.serial_set_line_coding = [](const t5_usb_line_coding_t* coding) {
    usbStatus.line_coding = *coding; return true;
  };
  usb.serial_set_control_lines = [](bool dtr, bool rts) {
    usbStatus.dtr = dtr; usbStatus.rts = rts; return true;
  };
  usbStatus.status = T5_USB_STATUS_READY;
  usbStatus.connected = 1;
  std::strcpy(usbStatus.product, "USB serial");
  usbStatus.vid = 0x1234; usbStatus.pid = 0x5678;

  // Real app launches create this context in NativeStreamBridge before the
  // serial bridge begins. A USB device lease must never use an arbitrary owner.
  RuntimeResources::ExecutionContext context;
  assert(context.begin());
  assert(!t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION));
  nativeSerialPortsBegin();
  const auto* api = t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION);
  assert(api && !t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION + 1));
  t5_serial_port_request_t request{};
  request.config = {115200, 8, T5_SERIAL_PARITY_NONE, 1, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t lease = 0, duplicate = 0;
  t5_stream_t rx = 0, tx = 0;
  assert(api->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && rx == 20 && tx == 30 && usbStarts == 1);
  assert(api->acquire(&request, &duplicate, &rx, &tx) == T5_SERIAL_BUSY && !duplicate);
  assert(!nativeRegisterSerialProvider(alternative())); // No registration during a live lease.
  nativeUsbProviderAttach(&usbStatus, 2);
  t5_serial_port_state_t state{};
  assert(api->read_status(lease, &state) == T5_SERIAL_OK && state.connected && state.device);
  const auto usbDevice = state.device;
  assert(api->release(lease) == T5_SERIAL_OK && usbStops == 1 && usbCloses == 2);
  assert(api->release(lease) == T5_SERIAL_CLOSED);

  assert(nativeRegisterSerialProvider(alternative()));
  assert(!nativeRegisterSerialProvider(alternative()));
  assert(!nativeUnregisterSerialProvider("usb.serial"));
  request.device = kAlternativeDevice;
  t5_serial_port_lease_t alternateLease = 0;
  assert(api->acquire(&request, &alternateLease, &rx, &tx) == T5_SERIAL_OK);
  assert(alternateLease && alternateLease != lease && rx == 41 && tx == 42);
  assert(fake.acquisitions == 1 && usbStarts == 1);
  assert(!nativeUnregisterSerialProvider("alternate.serial"));
  assert(api->read_status(alternateLease, &state) == T5_SERIAL_OK &&
         state.device == kAlternativeDevice && state.connected);
  auto changed = request.config;
  changed.baud_rate = 230400;
  changed.flow_control = T5_SERIAL_FLOW_RTS_CTS; // Not constrained by USB-only validation.
  assert(api->configure(alternateLease, &changed) == T5_SERIAL_OK && fake.configs == 1);
  assert(api->set_control_lines(alternateLease, true, false) == T5_SERIAL_OK && fake.controls == 1);
  assert(api->release(alternateLease) == T5_SERIAL_OK && fake.releases == 1 && usbStops == 1);
  assert(api->read_status(alternateLease, &state) == T5_SERIAL_CLOSED);

  // A default request selects the usable provider without naming USB.
  usbSupported = false;
  request.device = 0;
  t5_serial_port_lease_t fallback = 0;
  assert(api->acquire(&request, &fallback, &rx, &tx) == T5_SERIAL_OK);
  assert(rx == 41 && tx == 42 && usbStarts == 1);
  assert(api->release(fallback) == T5_SERIAL_OK && fake.releases == 2);
  usbSupported = true;

  // An old USB device selector cannot route to another provider after detach.
  request.device = usbDevice;
  assert(api->acquire(&request, &duplicate, &rx, &tx) == T5_SERIAL_INVALID && !duplicate);
  request.device = 0;
  assert(api->acquire(&request, &duplicate, &rx, &tx) == T5_SERIAL_OK);
  assert(duplicate != lease && duplicate != alternateLease && usbStarts == 2);
  nativeUsbProviderAttach(&usbStatus, 2);
  nativeUsbProviderDetach();
  assert(api->read_status(duplicate, &state) == T5_SERIAL_OK &&
         !state.connected && state.last_error == T5_SERIAL_DISCONNECTED);
  assert(api->release(duplicate) == T5_SERIAL_OK && usbStops == 2);

  // A selected provider survives one invocation but cannot retain its lease.
  request.device = kAlternativeDevice;
  t5_serial_port_lease_t ending = 0;
  assert(api->acquire(&request, &ending, &rx, &tx) == T5_SERIAL_OK);
  nativeSerialPortsEnd();
  assert(fake.releases == 3 && usbStops == 2 && !t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION));
  nativeSerialPortsBegin();
  assert(api->release(ending) == T5_SERIAL_CLOSED);
  assert(nativeUnregisterSerialProvider("alternate.serial"));
  assert(!nativeUnregisterSerialProvider("alternate.serial"));
  request.device = kAlternativeDevice;
  assert(api->acquire(&request, &duplicate, &rx, &tx) == T5_SERIAL_INVALID);
  nativeSerialPortsEnd();
  context.end();
  assert(!RuntimeResources::ExecutionContext::current());
  std::puts("Production serial provider dispatch, selection and teardown tests passed");
}
