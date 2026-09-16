#include "NativeSerialPortBridge.h"
#include "NativeStreamBridge.h"
#include "NativeUsbDeviceRegistry.h"
#include "runtime/capabilities/SerialProviderRegistry.h"
#include "runtime/capabilities/UsbSerialProjection.h"
#include "runtime/resources/ExecutionContext.h"
#include <T5AppApi.h>
#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include <cstdio>
#include <cstring>
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <Logging.h>
#endif

namespace {
constexpr uint32_t kMaxHandleGeneration = 0x7fffffffu;
const t5_usb_api_v1* usb = nullptr;
bool active = false;
t5_serial_port_lease_t leaseHandle = 0;
t5_stream_t rxHandle = 0;
t5_stream_t txHandle = 0;
uint32_t leaseGeneration = 0;
uint32_t leaseEpoch = 0;
// USB host callbacks update ONLY the legacy lock-protected snapshot. All
// unified registry mutations occur later on the application's owner task.
NativeUsbDevices::Registry devices;
RuntimeDevices::UsbSerialProjection usbProjection(RuntimeDevices::systemRegistry());
RuntimeDevices::LeaseHandle physicalLease = 0;
uint32_t physicalOwner = 0;
enum class UsbConsumer : uint8_t { None, SerialPort, DirectStream };
UsbConsumer physicalConsumer = UsbConsumer::None;
RuntimeSerial::Registry providers;
t5_serial_config_t currentConfig = {115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};

// Only status transitions and API calls are logged. Polls, received content,
// transmitted content and protocol payloads must not flood the debug channel.
t5_serial_port_lease_t lastLoggedLease = 0;
uint8_t lastLoggedStatus = 0xff;
uint8_t lastLoggedConnected = 0xff;
int32_t lastLoggedError = 0;
t5_serial_device_t lastLoggedDevice = 0;
void resetStatusTrace() {
  lastLoggedLease = 0;
  lastLoggedStatus = 0xff;
  lastLoggedConnected = 0xff;
  lastLoggedError = 0;
  lastLoggedDevice = 0;
}
void traceResult(const char* action, t5_serial_result_t result,
                 t5_serial_port_lease_t lease, t5_serial_device_t device) {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  LOG_INF("SERIAL", "SERREF action=%s result=%d lease=%lu device=%lu",
          action, static_cast<int>(result), static_cast<unsigned long>(lease),
          static_cast<unsigned long>(device));
#else
  (void)action; (void)result; (void)lease; (void)device;
#endif
}
void traceStatus(t5_serial_port_lease_t lease, const t5_serial_port_state_t& status) {
  if (lastLoggedLease == lease && lastLoggedStatus == status.status &&
      lastLoggedConnected == status.connected && lastLoggedError == status.last_error &&
      lastLoggedDevice == status.device) return;
  lastLoggedLease = lease;
  lastLoggedStatus = status.status;
  lastLoggedConnected = status.connected;
  lastLoggedError = status.last_error;
  lastLoggedDevice = status.device;
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  LOG_INF("SERIAL", "SERREF action=status lease=%lu device=%lu status=%u connected=%u error=%ld rx=%lu tx=%lu dropped=%lu",
          static_cast<unsigned long>(lease), static_cast<unsigned long>(status.device),
          static_cast<unsigned>(status.status), static_cast<unsigned>(status.connected),
          static_cast<long>(status.last_error), static_cast<unsigned long>(status.rx_bytes),
          static_cast<unsigned long>(status.tx_bytes),
          static_cast<unsigned long>(status.dropped_rx_bytes));
#else
  (void)lease; (void)status;
#endif
}

bool authorized() {
  return active && t5_app_get_api(T5_APP_ABI_VERSION) != nullptr;
}

// The host callback never touches systemRegistry(). A snapshot is projected
// only by the owner task; loss revokes the physical lease but intentionally
// keeps the session reservation until its original consumer closes it.
void synchronizeUsbDevice() {
  const auto snapshot = devices.diagnostics();
  const auto& d = snapshot.device;
  (void)usbProjection.reconcile(snapshot.epoch, d.id,
      d.presence == NativeUsbDevices::Presence::Bound,
      d.vid, d.pid, d.interface_number, d.product);
  if (physicalLease && !RuntimeDevices::systemRegistry().valid(physicalLease, physicalOwner)) {
    physicalLease = 0;
  }
}

void releasePhysicalDevice(UsbConsumer consumer) {
  if (physicalConsumer != consumer) return;
  if (physicalLease) {
    (void)RuntimeDevices::systemRegistry().release(physicalLease, physicalOwner);
    physicalLease = 0;
  }
  physicalOwner = 0;
  physicalConsumer = UsbConsumer::None;
}

// Both USB consumers reserve the same session even while the host is still
// enumerating. A bound interface additionally receives ONE exclusive
// serial.port lease, never a second lease under a different owner or API.
bool claimPhysicalDevice(UsbConsumer consumer) {
  synchronizeUsbDevice();
  auto* context = RuntimeResources::ExecutionContext::current();
  if (!context || !context->running(context->id())) return false;
  if (physicalConsumer != UsbConsumer::None &&
      (physicalConsumer != consumer || physicalOwner != context->id())) return false;
  if (physicalLease) return RuntimeDevices::systemRegistry().valid(physicalLease, context->id());
  if (!usbProjection.device()) {
    physicalConsumer = consumer;
    physicalOwner = context->id();
    return true;  // Initial host enumeration is asynchronous.
  }
  RuntimeDevices::LeaseHandle next = 0;
  if (RuntimeDevices::systemRegistry().acquire(
          "serial.port", context->id(), &next, usbProjection.device(),
          RuntimeDevices::Mode::Exclusive) != RuntimeDevices::Result::Ok) return false;
  physicalLease = next;
  physicalConsumer = consumer;
  physicalOwner = context->id();
  return true;
}

bool validConfig(const t5_serial_config_t* config) {
  return config && config->baud_rate >= 300u && config->baud_rate <= 3000000u &&
         config->data_bits >= 5u && config->data_bits <= 8u &&
         config->parity <= T5_SERIAL_PARITY_SPACE &&
         (config->stop_bits == 1u || config->stop_bits == 2u) &&
         config->flow_control == T5_SERIAL_FLOW_NONE;
}

t5_usb_line_coding_t toUsb(const t5_serial_config_t& config) {
  t5_usb_line_coding_t coding{};
  coding.baud_rate = config.baud_rate;
  coding.data_bits = config.data_bits;
  coding.parity = config.parity;
  coding.stop_bits = config.stop_bits;
  return coding;
}

uint8_t semanticStatus(uint8_t status) {
  switch (status) {
    case T5_USB_STATUS_OFF: return T5_SERIAL_STATUS_OFF;
    case T5_USB_STATUS_WAITING: return T5_SERIAL_STATUS_WAITING;
    case T5_USB_STATUS_CONFIGURING: return T5_SERIAL_STATUS_CONFIGURING;
    case T5_USB_STATUS_READY: return T5_SERIAL_STATUS_READY;
    case T5_USB_STATUS_ERROR: return T5_SERIAL_STATUS_ERROR;
    default: return T5_SERIAL_STATUS_UNAVAILABLE;
  }
}

bool validLease(t5_serial_port_lease_t lease) {
  return lease != 0 && lease == leaseHandle;
}

bool leaseRevoked() {
  return devices.epoch() != leaseEpoch || leaseEpoch == UINT32_MAX;
}

void clearLease(bool stopUsb) {
  if (rxHandle) (void)nativeStreamCloseOwned(rxHandle);
  if (txHandle) (void)nativeStreamCloseOwned(txHandle);
  rxHandle = txHandle = 0;
  releasePhysicalDevice(UsbConsumer::SerialPort);
  if (stopUsb && usb && usb->serial_stop) usb->serial_stop();
  leaseHandle = 0;
  leaseEpoch = 0;
  // Host stop and app unload invalidate even an identical VID/PID device.
  devices.detach();
  synchronizeUsbDevice();
}

// USB is one provider; its host/power/epoch/stream details stay private.
t5_serial_result_t usbAcquirePort(const t5_serial_port_request_t* request,
                                  t5_serial_port_lease_t* lease,
                                  t5_stream_t* rx,
                                  t5_stream_t* tx) {
  if (lease) *lease = 0;
  if (rx) *rx = 0;
  if (tx) *tx = 0;
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!request || !lease || !rx || !tx) return T5_SERIAL_INVALID;
  if (request->config.flow_control != T5_SERIAL_FLOW_NONE) return T5_SERIAL_UNSUPPORTED;
  if (!validConfig(&request->config)) return T5_SERIAL_INVALID;
  if (leaseHandle || nativeStreamUsbIsBusy()) return T5_SERIAL_BUSY;
  if (leaseGeneration >= kMaxHandleGeneration) return T5_SERIAL_LIMIT;
  if (request->device && !devices.resolve(request->device, NativeUsbDevices::Provider::UsbSerial))
    return T5_SERIAL_INVALID;

  usb = t5_usb_get_api(T5_USB_API_VERSION);
  if (!usb || !usb->supported || !usb->supported() || !usb->serial_start || !usb->serial_stop ||
      !usb->serial_read_state || !usb->serial_set_line_coding || !usb->serial_set_control_lines)
    return T5_SERIAL_UNSUPPORTED;

  const auto coding = toUsb(request->config);
  if (!usb->serial_start(&coding)) return T5_SERIAL_IO;
  const uint32_t startEpoch = devices.epoch();
  if (startEpoch == UINT32_MAX) { usb->serial_stop(); return T5_SERIAL_LIMIT; }

  t5_stream_t newRx = 0, newTx = 0;
  const auto streamResult = nativeStreamOpenUsbPair(&newRx, &newTx);
  if (streamResult != T5_STREAM_OK) {
    usb->serial_stop();
    devices.detach();
    synchronizeUsbDevice();
    if (streamResult == T5_STREAM_BUSY) return T5_SERIAL_BUSY;
    if (streamResult == T5_STREAM_UNSUPPORTED) return T5_SERIAL_UNSUPPORTED;
    if (streamResult == T5_STREAM_DENIED) return T5_SERIAL_DENIED;
    if (streamResult == T5_STREAM_LIMIT) return T5_SERIAL_LIMIT;
    return T5_SERIAL_IO;
  }
  // Reject detach during acquisition rather than issuing streams for another
  // physical device. Preserve the original epoch before teardown changes it.
  const bool detached = devices.epoch() != startEpoch;
  if (detached || !claimPhysicalDevice(UsbConsumer::SerialPort)) {
    (void)nativeStreamCloseOwned(newRx);
    (void)nativeStreamCloseOwned(newTx);
    releasePhysicalDevice(UsbConsumer::SerialPort);
    usb->serial_stop();
    devices.detach();
    synchronizeUsbDevice();
    return detached ? T5_SERIAL_DISCONNECTED : T5_SERIAL_BUSY;
  }

  ++leaseGeneration;
  leaseHandle = (leaseGeneration << 1u) | 1u;
  leaseEpoch = startEpoch;
  rxHandle = newRx;
  txHandle = newTx;
  currentConfig = request->config;
  *lease = leaseHandle;
  *rx = rxHandle;
  *tx = txHandle;
  return T5_SERIAL_OK;
}

t5_serial_result_t usbConfigurePort(t5_serial_port_lease_t lease, const t5_serial_config_t* config) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  synchronizeUsbDevice();
  if (leaseRevoked()) return T5_SERIAL_DISCONNECTED;
  if (!claimPhysicalDevice(UsbConsumer::SerialPort)) return T5_SERIAL_BUSY;
  if (config && config->flow_control != T5_SERIAL_FLOW_NONE) return T5_SERIAL_UNSUPPORTED;
  if (!validConfig(config)) return T5_SERIAL_INVALID;
  const auto coding = toUsb(*config);
  if (!usb->serial_set_line_coding(&coding)) return T5_SERIAL_IO;
  currentConfig = *config;
  return T5_SERIAL_OK;
}

t5_serial_result_t usbReadStatus(t5_serial_port_lease_t lease, t5_serial_port_state_t* out) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  if (!out) return T5_SERIAL_INVALID;
  std::memset(out, 0, sizeof(*out));
  synchronizeUsbDevice();
  if (leaseRevoked()) {
    out->status = T5_SERIAL_STATUS_WAITING;
    out->last_error = T5_SERIAL_DISCONNECTED;
    out->config = currentConfig;
    return T5_SERIAL_OK;
  }
  t5_usb_serial_state_t state{};
  if (!usb->serial_read_state(&state)) return T5_SERIAL_IO;
  const auto device = devices.snapshot();
  synchronizeUsbDevice();
  if (leaseRevoked()) {
    out->status = T5_SERIAL_STATUS_WAITING;
    out->last_error = T5_SERIAL_DISCONNECTED;
    out->config = currentConfig;
    return T5_SERIAL_OK;
  }
  const bool bound = device.presence == NativeUsbDevices::Presence::Bound;
  if (state.connected && bound && !claimPhysicalDevice(UsbConsumer::SerialPort)) return T5_SERIAL_BUSY;
  out->status = semanticStatus(state.status);
  out->connected = state.connected && bound;
  if (!bound && out->status != T5_SERIAL_STATUS_ERROR && out->status != T5_SERIAL_STATUS_OFF)
    out->status = T5_SERIAL_STATUS_WAITING;
  out->dtr = state.dtr;
  out->rts = state.rts;
  out->last_error = state.last_error;
  out->rx_bytes = state.rx_bytes;
  out->tx_bytes = state.tx_bytes;
  out->dropped_rx_bytes = state.dropped_rx_bytes;
  out->device = bound && state.connected ? device.id : 0;
  out->config = currentConfig;
  if (out->device && device.product[0]) {
    std::snprintf(out->device_label, sizeof(out->device_label), "%s  %04X:%04X",
                  device.product, (unsigned)device.vid, (unsigned)device.pid);
  } else if (out->device) {
    std::snprintf(out->device_label, sizeof(out->device_label), "Serial device  %04X:%04X",
                  (unsigned)device.vid, (unsigned)device.pid);
  }
  return T5_SERIAL_OK;
}

t5_serial_result_t usbSetControlLines(t5_serial_port_lease_t lease, bool dtr, bool rts) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  synchronizeUsbDevice();
  if (leaseRevoked()) return T5_SERIAL_DISCONNECTED;
  if (!claimPhysicalDevice(UsbConsumer::SerialPort)) return T5_SERIAL_BUSY;
  return usb->serial_set_control_lines(dtr, rts) ? T5_SERIAL_OK : T5_SERIAL_IO;
}

t5_serial_result_t usbReleasePort(t5_serial_port_lease_t lease) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  clearLease(true);
  return T5_SERIAL_OK;
}

bool usbAvailable(void*) {
  const auto* api = t5_usb_get_api(T5_USB_API_VERSION);
  return api && api->supported && api->supported() && api->serial_start &&
         api->serial_stop && api->serial_read_state && api->serial_set_line_coding &&
         api->serial_set_control_lines;
}
bool usbMatches(void*, t5_serial_device_t id) {
  return devices.resolve(id, NativeUsbDevices::Provider::UsbSerial);
}
t5_serial_result_t acquireUsb(void*, const t5_serial_port_request_t* request,
                              t5_serial_port_lease_t* lease, t5_stream_t* rx, t5_stream_t* tx) {
  return usbAcquirePort(request, lease, rx, tx);
}
t5_serial_result_t configureUsb(void*, t5_serial_port_lease_t lease, const t5_serial_config_t* config) {
  return usbConfigurePort(lease, config);
}
t5_serial_result_t statusUsb(void*, t5_serial_port_lease_t lease, t5_serial_port_state_t* state) {
  return usbReadStatus(lease, state);
}
t5_serial_result_t controlUsb(void*, t5_serial_port_lease_t lease, bool dtr, bool rts) {
  return usbSetControlLines(lease, dtr, rts);
}
t5_serial_result_t releaseUsb(void*, t5_serial_port_lease_t lease) {
  return usbReleasePort(lease);
}
const RuntimeSerial::Provider usbProvider = {
    "usb.serial", 0, nullptr, usbAvailable, usbMatches, acquireUsb, configureUsb,
    statusUsb, controlUsb, releaseUsb};
bool usbRegistered = false;
void ensureUsbRegistered() {
  if (!usbRegistered) usbRegistered = providers.add(usbProvider);
}

// Public ABI only dispatches through the semantic provider/lease resolver.
t5_serial_result_t acquirePort(const t5_serial_port_request_t* request,
                               t5_serial_port_lease_t* lease,
                               t5_stream_t* rx,
                               t5_stream_t* tx) {
  if (lease) *lease = 0;
  if (rx) *rx = 0;
  if (tx) *tx = 0;
  if (!authorized()) {
    traceResult("acquire", T5_SERIAL_DENIED, 0, request ? request->device : 0);
    return T5_SERIAL_DENIED;
  }
  synchronizeUsbDevice();
  ensureUsbRegistered();
  const auto rc = providers.acquire(request, lease, rx, tx);
  traceResult("acquire", rc, rc == T5_SERIAL_OK && lease ? *lease : 0,
              request ? request->device : 0);
  if (rc == T5_SERIAL_OK) resetStatusTrace();
  return rc;
}
t5_serial_result_t configurePort(t5_serial_port_lease_t lease, const t5_serial_config_t* config) {
  const auto rc = authorized() ? providers.configure(lease, config) : T5_SERIAL_DENIED;
  traceResult("configure", rc, lease, 0);
  return rc;
}
t5_serial_result_t readStatus(t5_serial_port_lease_t lease, t5_serial_port_state_t* state) {
  const auto rc = authorized() ? providers.status(lease, state) : T5_SERIAL_DENIED;
  if (rc != T5_SERIAL_OK) traceResult("read-status", rc, lease, 0);
  else if (state) traceStatus(lease, *state);
  return rc;
}
t5_serial_result_t setControlLines(t5_serial_port_lease_t lease, bool dtr, bool rts) {
  const auto rc = authorized() ? providers.control(lease, dtr, rts) : T5_SERIAL_DENIED;
  traceResult("control-lines", rc, lease, 0);
  return rc;
}
t5_serial_result_t releasePort(t5_serial_port_lease_t lease) {
  const auto rc = authorized() ? providers.release(lease) : T5_SERIAL_DENIED;
  traceResult("release", rc, lease, 0);
  if (rc == T5_SERIAL_OK) resetStatusTrace();
  return rc;
}

const t5_serial_port_api_v1 api = {
    T5_SERIAL_PORT_API_VERSION,
    sizeof(t5_serial_port_api_v1),
    T5_SERIAL_PORT_CAPABILITY,
    acquirePort,
    configurePort,
    readStatus,
    setControlLines,
    releasePort,
};
} // namespace

// Trusted registration only; applications cannot import these hooks.
// Register on the owning task with no active serial lease.
bool nativeRegisterSerialProvider(const RuntimeSerial::Provider& provider) {
  ensureUsbRegistered();
  return providers.add(provider);
}
bool nativeUnregisterSerialProvider(const char* id) {
  if (!id || std::strcmp(id, "usb.serial") == 0) return false;
  return providers.remove(id);
}

// USB host task publishes device identity separately from registration.
void nativeUsbProviderAttach(const t5_usb_serial_state_t* state, uint8_t dataInterface) {
  if (state) devices.observe(*state, dataInterface);
}
void nativeUsbProviderDetach() {
  devices.detach();
}
uint32_t nativeUsbProviderEpoch() {
  return devices.epoch();
}

// Called only on the owning application task, never the USB host callback or
// stream pump task. Direct streams and serial sessions use the same capability
// registry and the same exclusive physical-interface lease.
bool nativeUsbDirectStreamClaim(uint32_t expectedEpoch) {
  if (!authorized() || devices.epoch() != expectedEpoch) return false;
  if (!claimPhysicalDevice(UsbConsumer::DirectStream)) return false;
  return devices.epoch() == expectedEpoch;
}
void nativeUsbDirectStreamRelease() {
  releasePhysicalDevice(UsbConsumer::DirectStream);
}

void nativeSerialPortsBegin() {
  clearLease(false);
  usb = nullptr;
  active = true;
  resetStatusTrace();
  synchronizeUsbDevice();
  ensureUsbRegistered();
  traceResult("context-begin", T5_SERIAL_OK, 0, 0);
}
void nativeSerialPortsEnd() {
  // Release the selected provider before reclaiming streams. A non-USB
  // provider must never trigger an unrelated USB serial_stop.
  providers.end();
  if (leaseHandle) clearLease(true);
  releasePhysicalDevice(UsbConsumer::SerialPort);
  releasePhysicalDevice(UsbConsumer::DirectStream);
  devices.detach();
  synchronizeUsbDevice();
  active = false;
  usb = nullptr;
  resetStatusTrace();
  traceResult("context-end", T5_SERIAL_OK, 0, 0);
}

extern "C" const t5_serial_port_api_v1* t5_serial_port_get_api(uint32_t version) {
  if (version != T5_SERIAL_PORT_API_VERSION || !authorized()) return nullptr;
  return &api;
}
