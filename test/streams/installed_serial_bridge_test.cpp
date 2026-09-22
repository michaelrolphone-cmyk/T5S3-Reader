#include <T5AppApi.h>
#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include <RiscSerialPortV1.h>
#include "native/NativeSerialPortBridge.h"
#include "native/NativeUsbClassBridge.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include "runtime/resources/ExecutionContext.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
t5_app_api_v1 app{};
const char* ids[] = {"class.one", "class.four"};
uint64_t fourthToken = 44;
bool fourthUncertain = false;
unsigned graphAcquires = 0, graphReleases = 0;
unsigned providerOpens = 0, providerCloses = 0;\nuint64_t lastOpenedDevice = 0;
unsigned alternativeAcquires = 0, alternativeReleases = 0;
const risc_serial_port_api_v1* boundApi = nullptr;
uint64_t observedDevice = 0, classToken = 0;
bool classBound = false, classStarted = false, streamBusy = false;
bool dtr = false, rts = false;
t5_serial_config_t coding{115200, 8, T5_SERIAL_PARITY_NONE, 1, T5_SERIAL_FLOW_NONE};

uint64_t openPort(uint64_t device) { ++providerOpens; lastOpenedDevice = device; return device ? device + 1000u : 0; }
bool configurePort(uint64_t token, uint32_t baud, uint8_t bits,
                   uint8_t parity, uint8_t stops) {
  if (!token) return false;
  coding = {baud, bits, parity, stops, T5_SERIAL_FLOW_NONE};
  return true;
}
bool controlPort(uint64_t token, bool nextDtr, bool nextRts) {
  if (!token) return false;
  dtr = nextDtr; rts = nextRts; return true;
}
int32_t readPort(uint64_t, uint8_t*, size_t, uint32_t) { return 0; }
int32_t writePort(uint64_t, const uint8_t*, size_t, uint32_t) { return 0; }
bool closePort(uint64_t token) { if (!token) return false; ++providerCloses; return true; }
int32_t probe(uint64_t) { return 1; }

bool emptySnapshot(risc_serial_device_v1* out, size_t* count) {
  (void)out;
  if (!count) return false;
  *count = 0;
  return true;
}
bool fourthSnapshot(risc_serial_device_v1* out, size_t* count) {
  if (!count || fourthUncertain) return false;
  const size_t wanted = fourthToken ? 1u : 0u;
  if (*count < wanted || (wanted && !out)) { *count = wanted; return false; }
  if (wanted) out[0] = {fourthToken, fourthToken, RISC_SERIAL_TRANSPORT_USB, {}};
  *count = wanted;
  return true;
}
const risc_serial_port_inventory_v1 first = {
    {{RISC_SERIAL_PORT_API_V1, sizeof(risc_serial_port_inventory_v1),
      openPort, configurePort, controlPort, readPort, writePort, closePort},
     probe},
    emptySnapshot};
const risc_serial_port_inventory_v1 fourth = {
    {{RISC_SERIAL_PORT_API_V1, sizeof(risc_serial_port_inventory_v1),
      openPort, configurePort, controlPort, readPort, writePort, closePort},
     probe},
    fourthSnapshot};

constexpr t5_serial_device_t kAlternativeDevice = 0x80000001u;
bool altAvailable(void*) { return true; }
bool altMatches(void*, t5_serial_device_t id) { return id == kAlternativeDevice; }
t5_serial_result_t altAcquire(void*, const t5_serial_port_request_t*,
                              t5_serial_port_lease_t* lease,
                              t5_stream_t* rx, t5_stream_t* tx) {
  ++alternativeAcquires;
  *lease = 77; *rx = 201; *tx = 202;
  return T5_SERIAL_OK;
}
t5_serial_result_t altConfigure(void*, t5_serial_port_lease_t,
                                const t5_serial_config_t*) { return T5_SERIAL_OK; }
t5_serial_result_t altStatus(void*, t5_serial_port_lease_t,
                             t5_serial_port_state_t* state) {
  if (!state) return T5_SERIAL_INVALID;
  *state = {};
  state->status = T5_SERIAL_STATUS_READY;
  state->connected = 1;
  state->device = kAlternativeDevice;
  std::strcpy(state->device_label, "Alternative serial");
  return T5_SERIAL_OK;
}
t5_serial_result_t altControl(void*, t5_serial_port_lease_t, bool, bool) {
  return T5_SERIAL_OK;
}
t5_serial_result_t altRelease(void*, t5_serial_port_lease_t lease) {
  assert(lease == 77);
  ++alternativeReleases;
  return T5_SERIAL_OK;
}
RuntimeSerial::Provider alternativeProvider() {
  return {"alternative.serial", 10, nullptr, altAvailable, altMatches, altAcquire,
          altConfigure, altStatus, altControl, altRelease};
}
} // namespace

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  return version == T5_APP_ABI_VERSION ? &app : nullptr;
}

namespace RuntimeInstalledProviders {
bool prepare() { return true; }
bool nextProvider(const char* capability, uint32_t api, size_t* cursor,
                  char* id, size_t capacity) {
  assert(capability && !std::strcmp(capability, "serial.port") &&
         api == RISC_SERIAL_PORT_API_V1);
  if (!cursor || !id || capacity < 96 || *cursor >= 2) return false;
  std::snprintf(id, capacity, "%s", ids[(*cursor)++]);
  return true;
}
bool acquire(const char* id, const char* capability, uint32_t api, Lease* out) {
  assert(id && capability && !std::strcmp(capability, "serial.port") &&
         api == RISC_SERIAL_PORT_API_V1 && out);
  ++graphAcquires;
  *out = {};
  if (!std::strcmp(id, ids[0])) *out = {{1, graphAcquires}, &first.discovery.serial};
  if (!std::strcmp(id, ids[1])) *out = {{2, graphAcquires}, &fourth.discovery.serial};
  return out->grant.slot != 0;
}
bool release(Lease* lease) {
  assert(lease && lease->grant.slot);
  ++graphReleases;
  *lease = {};
  return true;
}
bool recoverFailedProvider(const char*, const char*, uint32_t) { return true; }
}

bool nativeUsbClassBound() { return classBound; }
bool nativeUsbClassBindApi(const void* api) {
  if (!api || classStarted || classToken) return false;
  boundApi = static_cast<const risc_serial_port_api_v1*>(api);
  classBound = true;
  return true;
}
void nativeUsbClassObserveDevice(uint64_t device) { observedDevice = device; }
bool nativeUsbClassStart(const t5_serial_config_t& config) {
  if (!classBound || !boundApi || !observedDevice || classStarted) return false;
  classToken = boundApi->open(observedDevice);
  if (!classToken || !boundApi->configure(classToken, config.baud_rate,
      config.data_bits, config.parity, config.stop_bits)) return false;
  classStarted = true;
  coding = config;
  return true;
}
bool nativeUsbClassStopChecked() {
  if (!classToken) { classStarted = false; return true; }
  if (!boundApi || !boundApi->close(classToken)) return false;
  classToken = 0;
  classStarted = false;
  return true;
}
bool nativeUsbClassUnbindChecked() {
  if (!nativeUsbClassStopChecked()) return false;
  boundApi = nullptr; observedDevice = 0; classBound = false;
  return true;
}
bool nativeUsbClassConfigure(const t5_serial_config_t& config) {
  return classStarted && boundApi &&
      boundApi->configure(classToken, config.baud_rate, config.data_bits,
                          config.parity, config.stop_bits);
}
bool nativeUsbClassControl(bool nextDtr, bool nextRts) {
  return classStarted && boundApi &&
      boundApi->control_lines(classToken, nextDtr, nextRts);
}
bool nativeUsbClassReadState(t5_usb_serial_state_t* state) {
  if (!state) return false;
  *state = {};
  state->status = classStarted ? T5_USB_STATUS_READY : T5_USB_STATUS_OFF;
  state->connected = classStarted ? 1 : 0;
  state->dtr = dtr; state->rts = rts;
  state->line_coding = {coding.baud_rate, coding.data_bits, coding.parity,
                        coding.stop_bits, 0};
  return true;
}
bool nativeUsbClassAttachPair(uint32_t owner, t5_stream_t rx, t5_stream_t tx) {
  return classStarted && owner && rx && tx && rx != tx;
}
uint64_t nativeUsbClassToken() { return classToken; }

bool nativeStreamSerialIsBusy() { return streamBusy; }
t5_stream_result_t nativeStreamOpenSerialPair(t5_stream_t* rx, t5_stream_t* tx) {
  if (!rx || !tx || streamBusy) return T5_STREAM_BUSY;
  *rx = 101; *tx = 102; streamBusy = true;
  return T5_STREAM_OK;
}
t5_stream_result_t nativeStreamCloseOwned(t5_stream_t stream) {
  if (stream != 101 && stream != 102) return T5_STREAM_INVALID;
  static unsigned closes = 0;
  if (++closes % 2u == 0u) streamBusy = false;
  return T5_STREAM_OK;
}

int main() {
  RuntimeResources::ExecutionContext context;
  assert(context.begin());
  nativeSerialPortsBegin();
  const auto* serial = t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION);
  assert(serial);
  nativeDeviceDiscoveryTick();

  auto& registry = RuntimeDevices::systemRegistry();
  assert(registry.count() == 1);
  RuntimeDevices::DeviceInfo published{};
  bool found = false;
  for (size_t i = 0; i < RuntimeDevices::kMaxDevices; ++i) {
    if (registry.at(i, &published)) { found = true; break; }
  }
  assert(found && !std::strcmp(published.provider, "class.four") &&
         !std::strcmp(published.capabilities[0], "serial.port"));

  t5_serial_port_request_t request{};
  request.config = {115200, 8, T5_SERIAL_PARITY_NONE, 1, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t lease = 0;
  t5_stream_t rx = 0, tx = 0;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && rx == 101 && tx == 102 && providerOpens == 1 &&
         lastOpenedDevice == 44);
  // The old USB class bridge was not involved in exact semantic binding.
  assert(boundApi == nullptr && observedDevice == 0 && !classBound);
  t5_serial_port_state_t status{};
  assert(serial->read_status(lease, &status) == T5_SERIAL_OK &&
         status.connected && status.device == published.handle &&
         !std::strcmp(status.device_label, "class.four"));
  assert(serial->release(lease) == T5_SERIAL_OK && providerCloses == 1);

  // With a still-published device, uncertain discovery must block fallback.
  assert(nativeRegisterSerialProvider(alternativeProvider()));
  fourthUncertain = true;
  lease = rx = tx = 0;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_IO);
  assert(!lease && !rx && !tx && alternativeAcquires == 0);
  fourthUncertain = false;

  // A verified empty installed inventory may now yield to another transport.
  fourthToken = 0;
  nativeDeviceDiscoveryTick();
  assert(registry.count() == 0);
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lease && rx == 201 && tx == 202 && alternativeAcquires == 1);
  assert(serial->release(lease) == T5_SERIAL_OK && alternativeReleases == 1);

  request.device = published.handle; // old app handle cannot resurrect the device.
  lease = rx = tx = 0;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_INVALID);
  assert(!lease && !rx && !tx);

  nativeSerialPortsEnd();
  context.end();
  assert(!RuntimeResources::ExecutionContext::current());
  assert(graphAcquires == 2 && graphReleases == 0); // inventory pins providers
  std::puts("Installed serial bridge binds exact provider and fail-closes uncertainty: PASS");
}
