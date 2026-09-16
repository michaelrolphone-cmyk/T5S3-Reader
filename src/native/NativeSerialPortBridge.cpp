#include "NativeSerialPortBridge.h"
#include "NativeStreamBridge.h"
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
uint32_t deviceGeneration = 0;
bool devicePresent = false;
t5_serial_device_t deviceHandle = 0;
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

void updateDeviceHandle(bool present) {
  if (present == devicePresent) return;
  devicePresent = present;
  if (!present) {
    deviceHandle = 0;
    return;
  }
  // Handle generations never wrap: once exhausted, this execution lifetime can
  // still report connection state but cannot issue an identity that could alias
  // an older device handle.
  if (deviceGeneration >= kMaxHandleGeneration) {
    deviceHandle = 0;
    return;
  }
  ++deviceGeneration;
  deviceHandle = (deviceGeneration << 1u) | 1u;
}

void clearLease(bool stopUsb) {
  if (rxHandle) (void)nativeStreamCloseOwned(rxHandle);
  if (txHandle) (void)nativeStreamCloseOwned(txHandle);
  rxHandle = txHandle = 0;
  if (stopUsb && usb && usb->serial_stop) usb->serial_stop();
  leaseHandle = 0;
  updateDeviceHandle(false);
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
  if (request->device != 0 && request->device != deviceHandle) return T5_SERIAL_INVALID;
  if (leaseHandle) return T5_SERIAL_BUSY;
  if (leaseGeneration >= kMaxHandleGeneration) return T5_SERIAL_LIMIT;

  usb = t5_usb_get_api(T5_USB_API_VERSION);
  if (!usb || !usb->supported || !usb->supported() || !usb->serial_start || !usb->serial_stop ||
      !usb->serial_read_state || !usb->serial_set_line_coding || !usb->serial_set_control_lines)
    return T5_SERIAL_UNSUPPORTED;

  const auto coding = toUsb(request->config);
  if (!usb->serial_start(&coding)) return T5_SERIAL_IO;

  t5_stream_t newRx = 0, newTx = 0;
  const auto streamResult = nativeStreamOpenUsbPair(&newRx, &newTx);
  if (streamResult != T5_STREAM_OK) {
    usb->serial_stop();
    if (streamResult == T5_STREAM_BUSY) return T5_SERIAL_BUSY;
    if (streamResult == T5_STREAM_UNSUPPORTED) return T5_SERIAL_UNSUPPORTED;
    if (streamResult == T5_STREAM_DENIED) return T5_SERIAL_DENIED;
    if (streamResult == T5_STREAM_LIMIT) return T5_SERIAL_LIMIT;
    return T5_SERIAL_IO;
  }

  ++leaseGeneration;
  leaseHandle = (leaseGeneration << 1u) | 1u;
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
  t5_usb_serial_state_t state{};
  if (!usb->serial_read_state(&state)) return T5_SERIAL_IO;

  std::memset(out, 0, sizeof(*out));
  updateDeviceHandle(state.connected != 0);
  out->status = semanticStatus(state.status);
  out->connected = state.connected;
  out->dtr = state.dtr;
  out->rts = state.rts;
  out->last_error = state.last_error;
  out->rx_bytes = state.rx_bytes;
  out->tx_bytes = state.tx_bytes;
  out->dropped_rx_bytes = state.dropped_rx_bytes;
  out->device = deviceHandle;
  out->config = currentConfig;
  if (state.product[0] && state.connected) {
    std::snprintf(out->device_label, sizeof(out->device_label), "%s  %04X:%04X",
                  state.product, (unsigned)state.vid, (unsigned)state.pid);
  } else if (state.connected) {
    std::snprintf(out->device_label, sizeof(out->device_label), "Serial device  %04X:%04X",
                  (unsigned)state.vid, (unsigned)state.pid);
  }
  return T5_SERIAL_OK;
}

t5_serial_result_t setControlLines(t5_serial_port_lease_t lease, bool dtr, bool rts) {
  if (!authorized()) return T5_SERIAL_DENIED;
  if (!validLease(lease)) return T5_SERIAL_CLOSED;
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

void nativeSerialPortsBegin() {
  clearLease(false);
  usb = nullptr;
  active = true;
}

void nativeSerialPortsEnd() {
  if (leaseHandle) clearLease(true);
  active = false;
  usb = nullptr;
}

extern "C" const t5_serial_port_api_v1* t5_serial_port_get_api(uint32_t version) {
  if (version != T5_SERIAL_PORT_API_VERSION || !authorized()) return nullptr;
  return &api;
}
