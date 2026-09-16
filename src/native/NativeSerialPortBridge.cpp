#include "NativeSerialPortBridge.h"
#include "NativeStreamBridge.h"
#include "NativeUsbDeviceRegistry.h"
#include <T5AppApi.h>
#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint32_t kMaxHandleGeneration = 0x7fffffffu;
const t5_usb_api_v1* usb = nullptr;
bool active = false;
t5_serial_port_lease_t leaseHandle = 0;
t5_stream_t rxHandle = 0;
t5_stream_t txHandle = 0;
uint32_t leaseGeneration = 0;
uint32_t leaseEpoch = 0;
// This registry and its generation persist across application contexts. The
// host owns publication; applications can only snapshot/resolve opaque IDs.
NativeUsbDevices::Registry devices;
t5_serial_config_t currentConfig = {115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};

bool authorized() {
  return active && t5_app_get_api(T5_APP_ABI_VERSION) != nullptr;
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
  if (stopUsb && usb && usb->serial_stop) usb->serial_stop();
  leaseHandle = 0;
  leaseEpoch = 0;
  // Host stop and app unload invalidate even a device with identical VID/PID.
  devices.detach();
}

t5_serial_result_t acquirePort(const t5_serial_port_request_t* request,
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
    if (streamResult == T5_STREAM_BUSY) return T5_SERIAL_BUSY;
    if (streamResult == T5_STREAM_UNSUPPORTED) return T5_SERIAL_UNSUPPORTED;
    if (streamResult == T5_STREAM_DENIED) return T5_SERIAL_DENIED;
    if (streamResult == T5_STREAM_LIMIT) return T5_SERIAL_LIMIT;
    return T5_SERIAL_IO;
  }
  // Reject a detach during acquisition rather than returning streams bound to
  // a different physical device than the newly issued lease.
  if (devices.epoch() != startEpoch) {
    (void)nativeStreamCloseOwned(newRx);
    (void)nativeStreamCloseOwned(newTx);
    usb->serial_stop();
    return T5_SERIAL_DISCONNECTED;
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

t5_serial_result_t configurePort(t5_serial_port_lease_t lease, const t5_serial_config_t* config) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  if (leaseRevoked()) return T5_SERIAL_DISCONNECTED;
  if (config && config->flow_control != T5_SERIAL_FLOW_NONE) return T5_SERIAL_UNSUPPORTED;
  if (!validConfig(config)) return T5_SERIAL_INVALID;
  const auto coding = toUsb(*config);
  if (!usb->serial_set_line_coding(&coding)) return T5_SERIAL_IO;
  currentConfig = *config;
  return T5_SERIAL_OK;
}

t5_serial_result_t readStatus(t5_serial_port_lease_t lease, t5_serial_port_state_t* out) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  if (!out) return T5_SERIAL_INVALID;
  std::memset(out, 0, sizeof(*out));
  if (leaseRevoked()) {
    // The lease cannot silently bind to a newly enumerated, identical device.
    // Give clients semantic disconnected status while retaining a valid handle
    // for explicit release; no USB details escape this capability.
    out->status = T5_SERIAL_STATUS_WAITING;
    out->last_error = T5_SERIAL_DISCONNECTED;
    out->config = currentConfig;
    return T5_SERIAL_OK;
  }
  t5_usb_serial_state_t state{};
  if (!usb->serial_read_state(&state)) return T5_SERIAL_IO;

  // Do NOT observe/re-create identities from this potentially stale snapshot:
  // DEV_GONE may already have invalidated an old ID on the USB host task while
  // the legacy USB status still reports the old connected device.
  const auto device = devices.snapshot();
  if (leaseRevoked()) {
    out->status = T5_SERIAL_STATUS_WAITING;
    out->last_error = T5_SERIAL_DISCONNECTED;
    out->config = currentConfig;
    return T5_SERIAL_OK;
  }
  const bool bound = device.presence == NativeUsbDevices::Presence::Bound;
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

t5_serial_result_t setControlLines(t5_serial_port_lease_t lease, bool dtr, bool rts) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  if (leaseRevoked()) return T5_SERIAL_DISCONNECTED;
  return usb->serial_set_control_lines(dtr, rts) ? T5_SERIAL_OK : T5_SERIAL_IO;
}

t5_serial_result_t releasePort(t5_serial_port_lease_t lease) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
  clearLease(true);
  return T5_SERIAL_OK;
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

// Called by the USB host task, never by a native ELF app. Only publish a
// supported/claimed serial interface, not every enumerated USB device.
void nativeUsbProviderAttach(const t5_usb_serial_state_t* state, uint8_t dataInterface) {
  if (state) devices.observe(*state, dataInterface);
}

void nativeUsbProviderDetach() {
  devices.detach();
}

uint32_t nativeUsbProviderEpoch() {
  return devices.epoch();
}

void nativeSerialPortsBegin() {
  clearLease(false);
  usb = nullptr;
  active = true;
}

void nativeSerialPortsEnd() {
  if (leaseHandle) clearLease(true);
  devices.detach();
  active = false;
  usb = nullptr;
}

extern "C" const t5_serial_port_api_v1* t5_serial_port_get_api(uint32_t version) {
  if (version != T5_SERIAL_PORT_API_VERSION || !authorized()) return nullptr;
  return &api;
}
