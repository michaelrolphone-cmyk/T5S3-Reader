#include "NativeUsbClassBridge.h"
#include <cstring>

namespace {
NativeUsbClassOps ops{};
uint64_t token = 0;
t5_serial_config_t coding{115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};
bool dtr = false, rts = false;
bool started = false;

bool valid(const NativeUsbClassOps& o) {
  return o.open && o.configure && o.control && o.close;
}
}  // namespace

bool nativeUsbClassBind(const NativeUsbClassOps& incoming) {
  if (started || token || !valid(incoming)) return false;
  ops = incoming;
  return true;
}

void nativeUsbClassUnbind() {
  nativeUsbClassStop();
  ops = {};
}

bool nativeUsbClassAvailable() { return valid(ops); }

bool nativeUsbClassStart(const t5_serial_config_t& config) {
  if (!valid(ops) || started) return false;
  if (config.baud_rate < 300u || config.baud_rate > 3000000u ||
      config.data_bits < 5u || config.data_bits > 8u ||
      (config.stop_bits != 1u && config.stop_bits != 2u)) return false;
  const uint64_t opened = ops.open(ops.context, 1);
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
  if (token && ops.close) (void)ops.close(ops.context, token);
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
