// Production serial provider + production class-ELF control plane. No T5UsbApi.
#include <T5AppApi.h>
#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include <RiscUsbProviderV1.h>
#include "native/NativeSerialPortBridge.h"
#include "native/NativeStreamBridge.h"
#include "native/NativeUsbClassBridge.h"
#include "native/NativeUsbDeviceRegistry.h"
#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
t5_app_api_v1 app{};
int opens = 0, closes = 0, configs = 0, controls = 0;
uint64_t token = 0;
bool streamBusy = false;
int streamCloses = 0;

uint64_t classOpen(void*, uint64_t device) {
  if (!device || token) return 0;
  ++opens;
  token = 7;
  return token;
}
bool classConfigure(void*, uint64_t t, uint32_t baud, uint8_t, uint8_t, uint8_t) {
  if (t != token || baud < 300) return false;
  ++configs;
  return true;
}
bool classControl(void*, uint64_t t, bool, bool) {
  if (t != token) return false;
  ++controls;
  return true;
}
int32_t classRead(void*, uint64_t t, uint8_t* bytes, uint32_t capacity, uint32_t) {
  if (t != token || !bytes || !capacity) return -1;
  return 0;
}
int32_t classWrite(void*, uint64_t t, const uint8_t* bytes, uint32_t count, uint32_t) {
  if (t != token || !bytes || !count) return -1;
  return static_cast<int32_t>(count);
}
bool classClose(void*, uint64_t t) {
  if (t != token) return false;
  ++closes;
  token = 0;
  return true;
}
}  // namespace

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  return version == T5_APP_ABI_VERSION ? &app : nullptr;
}
extern "C" const t5_usb_api_v1* t5_usb_get_api(uint32_t) { return nullptr; }
bool nativeStreamUsbIsBusy() { return streamBusy; }
t5_stream_result_t nativeStreamOpenUsbPair(t5_stream_t* rx, t5_stream_t* tx) {
  if (!rx || !tx || streamBusy) return T5_STREAM_BUSY;
  *rx = 3;
  *tx = 4;
  streamBusy = true;
  return T5_STREAM_OK;
}
t5_stream_result_t nativeStreamCloseOwned(t5_stream_t) {
  if (++streamCloses % 2 == 0) streamBusy = false;
  return T5_STREAM_OK;
}

int main() {
  assert(!nativeUsbClassAvailable());
  NativeUsbClassOps ops{nullptr, classOpen, classConfigure, classControl,
                        classRead, classWrite, classClose};
  assert(nativeUsbClassBind(ops));
  assert(nativeUsbClassAvailable() && nativeUsbClassHasDataPlane());

  RuntimeResources::ExecutionContext context;
  assert(context.begin());
  nativeSerialPortsBegin();
  const auto* api = t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION);
  assert(api);
  t5_serial_port_request_t request{};
  request.config = {115200, 8, T5_SERIAL_PARITY_NONE, 1, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t lease = 0;
  t5_stream_t rx = 0, tx = 0;
  assert(api->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && rx == 3 && tx == 4 && opens == 1 && configs == 1);
  assert(nativeUsbClassSession().token() == token &&
         nativeUsbClassSession().rx() == rx && nativeUsbClassSession().tx() == tx);
  assert(api->set_control_lines(lease, true, true) == T5_SERIAL_OK && controls == 1);
  auto next = request.config;
  next.baud_rate = 230400;
  assert(api->configure(lease, &next) == T5_SERIAL_OK && configs == 2);
  assert(api->release(lease) == T5_SERIAL_OK && closes == 1);
  nativeSerialPortsEnd();
  context.end();
  nativeUsbClassUnbind();
  assert(!nativeUsbClassAvailable());
  risc_usb_cdc_api_v1 cdcApi{};
  cdcApi.api_version = RISC_USB_CDC_API_V1;
  cdcApi.struct_size = sizeof(cdcApi);
  cdcApi.open = [](uint64_t d) -> uint64_t { return d ? 9u : 0u; };
  cdcApi.configure = [](uint64_t, uint32_t, uint8_t, uint8_t, uint8_t) { return true; };
  cdcApi.control_lines = [](uint64_t, bool, bool) { return true; };
  cdcApi.read = [](uint64_t, uint8_t*, size_t, uint32_t) -> int32_t { return 0; };
  cdcApi.write = [](uint64_t, const uint8_t*, size_t, uint32_t) -> int32_t { return 0; };
  cdcApi.close = [](uint64_t) { return true; };
  assert(nativeUsbClassBindApi(&cdcApi));
  assert(nativeUsbClassHasDataPlane());
  assert(nativeUsbClassEnsureInstalled());
  t5_serial_config_t cfg{115200, 8, T5_SERIAL_PARITY_NONE, 1, T5_SERIAL_FLOW_NONE};
  assert(nativeUsbClassStart(cfg));
  assert(nativeUsbClassToken());
  assert(nativeUsbClassAttachPair(1, 3, 4));
  nativeUsbClassStop();
  nativeUsbClassUnbind();
  std::puts("usb class control/data plane bound; T5UsbApi unused");
  return 0;
}
