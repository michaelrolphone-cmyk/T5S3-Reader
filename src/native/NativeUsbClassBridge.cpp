#include "NativeUsbClassBridge.h"
#include <RiscUsbProviderV1.h>
#if defined(ESP_PLATFORM)
#include "runtime/drivers/InstalledProviderGraph.h"
#endif
#include <cstring>

namespace {
NativeUsbClassOps ops{};
RuntimeUsb::ClassStreamSession session;
uint64_t token = 0;
uint64_t observedDevice = 1;
t5_serial_config_t coding{115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};
bool dtr = false, rts = false;
bool started = false;
const risc_usb_cdc_api_v1* boundApi = nullptr;
#if defined(ESP_PLATFORM)
RuntimeInstalledProviders::Lease installedClass{};
#endif

bool valid(const NativeUsbClassOps& o) {
  return o.open && o.configure && o.control && o.close;
}

uint64_t apiOpen(void* ctx, uint64_t device) {
  auto* api = static_cast<const risc_usb_cdc_api_v1*>(ctx);
  return api && api->open ? api->open(device) : 0;
}
bool apiConfigure(void* ctx, uint64_t t, uint32_t baud, uint8_t bits,
                  uint8_t parity, uint8_t stop) {
  auto* api = static_cast<const risc_usb_cdc_api_v1*>(ctx);
  return api && api->configure && api->configure(t, baud, bits, parity, stop);
}
bool apiControl(void* ctx, uint64_t t, bool nextDtr, bool nextRts) {
  auto* api = static_cast<const risc_usb_cdc_api_v1*>(ctx);
  return api && api->control_lines && api->control_lines(t, nextDtr, nextRts);
}
int32_t apiRead(void* ctx, uint64_t t, uint8_t* dst, uint32_t cap, uint32_t timeout) {
  auto* api = static_cast<const risc_usb_cdc_api_v1*>(ctx);
  if (!api || !api->read || !dst || !cap) return -1;
  return api->read(t, dst, cap, timeout);
}
int32_t apiWrite(void* ctx, uint64_t t, const uint8_t* src, uint32_t len, uint32_t timeout) {
  auto* api = static_cast<const risc_usb_cdc_api_v1*>(ctx);
  if (!api || !api->write || !src || !len) return -1;
  return api->write(t, src, len, timeout);
}
bool apiClose(void* ctx, uint64_t t) {
  auto* api = static_cast<const risc_usb_cdc_api_v1*>(ctx);
  return api && api->close && api->close(t);
}

bool apiValid(const risc_usb_cdc_api_v1* api) {
  return api && api->api_version == RISC_USB_CDC_API_V1 &&
         api->struct_size >= sizeof(risc_usb_cdc_api_v1) && api->open &&
         api->configure && api->control_lines && api->read && api->write &&
         api->close;
}

RuntimeUsb::ClassPort portFromOps(const NativeUsbClassOps& o) {
  return {o.context, o.open, o.configure, o.control, o.read, o.write, o.close};
}
}  // namespace

bool nativeUsbClassBind(const NativeUsbClassOps& incoming) {
  if (started || token || !valid(incoming)) return false;
  ops = incoming;
  session.unbind();
  if (incoming.read && incoming.write) (void)session.bind(portFromOps(incoming));
  return true;
}

bool nativeUsbClassBindPort(const RuntimeUsb::ClassPort& port) {
  NativeUsbClassOps incoming{port.context, port.open, port.configure, port.control,
                             port.read, port.write, port.close};
  return nativeUsbClassBind(incoming);
}

bool nativeUsbClassBindApi(const void* riscUsbCdcApiV1) {
  const auto* api = static_cast<const risc_usb_cdc_api_v1*>(riscUsbCdcApiV1);
  if (!apiValid(api)) return false;
  NativeUsbClassOps incoming{const_cast<risc_usb_cdc_api_v1*>(api),
                             apiOpen, apiConfigure, apiControl, apiRead, apiWrite,
                             apiClose};
  if (!nativeUsbClassBind(incoming)) return false;
  boundApi = api;
  return true;
}

void nativeUsbClassUnbind() {
  nativeUsbClassStop();
  session.unbind();
  ops = {};
  boundApi = nullptr;
  observedDevice = 1;
#if defined(ESP_PLATFORM)
  if (installedClass.grant.slot) (void)RuntimeInstalledProviders::release(&installedClass);
#endif
}

bool nativeUsbClassAvailable() { return valid(ops); }
bool nativeUsbClassHasDataPlane() { return session.bound(); }
uint64_t nativeUsbClassToken() { return token; }

void nativeUsbClassObserveDevice(uint64_t device) {
  if (device) observedDevice = device;
}

bool nativeUsbClassAdopt(uint64_t opened, const t5_serial_config_t& config) {
  if (!valid(ops) || !opened) return false;
  if (started && token != opened) return false;
  token = opened;
  coding = config;
  started = true;
  return true;
}

bool nativeUsbClassStart(const t5_serial_config_t& config) {
  if (!valid(ops)) return false;
  if (started) return token != 0;
  if (config.baud_rate < 300u || config.baud_rate > 3000000u ||
      config.data_bits < 5u || config.data_bits > 8u ||
      (config.stop_bits != 1u && config.stop_bits != 2u)) return false;
  const uint64_t opened = ops.open(ops.context, observedDevice ? observedDevice : 1);
  if (!opened) return false;
  if (!ops.configure(ops.context, opened, config.baud_rate, config.data_bits,
                     config.parity, config.stop_bits)) {
    (void)ops.close(ops.context, opened);
    return false;
  }
  token = opened;
  coding = config;
  started = true;
  return true;
}

void nativeUsbClassStop() {
  if (session.token()) {
    (void)session.close(nullptr);
  } else if (token && ops.close) {
    (void)ops.close(ops.context, token);
  }
  token = 0;
  started = false;
  dtr = rts = false;
}

bool nativeUsbClassConfigure(const t5_serial_config_t& config) {
  if (!started || !token) return false;
  if (!ops.configure(ops.context, token, config.baud_rate, config.data_bits,
                     config.parity, config.stop_bits)) return false;
  coding = config;
  return true;
}

bool nativeUsbClassControl(bool nextDtr, bool nextRts) {
  if (!started || !token) return false;
  if (!ops.control(ops.context, token, nextDtr, nextRts)) return false;
  dtr = nextDtr;
  rts = nextRts;
  return true;
}

bool nativeUsbClassReadState(t5_usb_serial_state_t* out) {
  if (!out) return false;
  std::memset(out, 0, sizeof(*out));
  out->status = started ? T5_USB_STATUS_READY : T5_USB_STATUS_OFF;
  out->connected = started ? 1 : 0;
  out->dtr = dtr;
  out->rts = rts;
  out->line_coding.baud_rate = coding.baud_rate;
  out->line_coding.data_bits = coding.data_bits;
  out->line_coding.parity = coding.parity;
  out->line_coding.stop_bits = coding.stop_bits;
  return true;
}

int32_t nativeUsbClassRead(uint8_t* dst, uint32_t capacity, uint32_t* out) {
  if (out) *out = 0;
  if (!dst || !capacity) return T5_STREAM_INVALID;
  if (!started || !token) return T5_STREAM_CLOSED;
  if (!ops.read) return T5_STREAM_AGAIN;
  const int32_t n = ops.read(ops.context, token, dst, capacity, 1);
  if (n < 0) return T5_STREAM_IO;
  if (out) *out = static_cast<uint32_t>(n);
  return n ? T5_STREAM_OK : T5_STREAM_AGAIN;
}

int32_t nativeUsbClassWrite(const uint8_t* src, uint32_t length, uint32_t* out) {
  if (out) *out = 0;
  if (!src || !length) return T5_STREAM_INVALID;
  if (!started || !token) return T5_STREAM_CLOSED;
  if (!ops.write) return T5_STREAM_AGAIN;
  const int32_t n = ops.write(ops.context, token, src, length, 1);
  if (n < 0) return T5_STREAM_IO;
  if (out) *out = static_cast<uint32_t>(n);
  return n ? T5_STREAM_OK : T5_STREAM_AGAIN;
}

RuntimeUsb::ClassStreamSession& nativeUsbClassSession() { return session; }

bool nativeUsbClassAttachPair(uint32_t owner, t5_stream_t rx, t5_stream_t tx) {
  if (!session.bound() || !token || !owner || !rx || !tx) return false;
  return session.attachPublished(owner, token, rx, tx) == T5_STREAM_OK;
}

void nativeUsbClassPump(RuntimeStreams::Registry& registry) {
  if (session.bound() && session.token()) (void)session.pump(registry);
}

#if defined(ESP_PLATFORM)
bool nativeUsbClassEnsureInstalled(uint16_t vid) {
  if (nativeUsbClassAvailable()) return true;
  const char* first = vid == 0x10c4u ? "usb-cp210x-v2" : "usb-cdc-acm-v2";
  const char* second = vid == 0x10c4u ? "usb-cdc-acm-v2" : "usb-cp210x-v2";
  const char* ids[] = {first, second};
  for (const char* id : ids) {
    RuntimeInstalledProviders::Lease grant{};
    if (!RuntimeInstalledProviders::acquire(id, "serial.port", 1, &grant)) continue;
    if (!nativeUsbClassBindApi(grant.interface)) {
      (void)RuntimeInstalledProviders::release(&grant);
      continue;
    }
    installedClass = grant;
    return true;
  }
  return false;
}
#else
bool nativeUsbClassEnsureInstalled(uint16_t) { return nativeUsbClassAvailable(); }
#endif
