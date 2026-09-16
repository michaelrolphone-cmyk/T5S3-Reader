#pragma once

#include <T5SerialPortApi.h>
#include <T5StreamApi.h>

// Transport-neutral identity watch. Latch the first claimed device while it
// is CONFIGURING, not only after it reaches READY: a detach during control
// setup must terminate programming rather than waiting for a new device.
// Absence before any claimed device is observed is still a connection timeout.
namespace EspRomSession {

enum class Observation : uint8_t { Waiting, Ready, Lost, Error };

class Watch final {
 public:
  Observation observe(t5_serial_result_t rc, const t5_serial_port_state_t* state) {
    if (rc == T5_SERIAL_DISCONNECTED || rc == T5_SERIAL_CLOSED) return Observation::Lost;
    if (rc != T5_SERIAL_OK || !state) return Observation::Error;
    // The provider reports a revoked epoch through last_error even when its
    // read_status call succeeds to allow clients to release the old lease.
    if (state->last_error == T5_SERIAL_DISCONNECTED) return Observation::Lost;
    if (device_ && (!state->connected || state->device != device_)) return Observation::Lost;
    if (state->connected && state->device && !device_) device_ = state->device;
    if (state->status == T5_SERIAL_STATUS_OFF && device_) return Observation::Lost;
    if (state->status == T5_SERIAL_STATUS_ERROR ||
        state->status == T5_SERIAL_STATUS_UNAVAILABLE) return Observation::Error;
    return state->status == T5_SERIAL_STATUS_READY && state->connected && state->device
        ? Observation::Ready : Observation::Waiting;
  }

  t5_serial_device_t device() const { return device_; }

  static bool lostControl(t5_serial_result_t rc) {
    return rc == T5_SERIAL_DISCONNECTED || rc == T5_SERIAL_CLOSED;
  }

  static bool lostStream(t5_stream_result_t rc) {
    return rc == T5_STREAM_DISCONNECTED || rc == T5_STREAM_CLOSED || rc == T5_STREAM_EOF;
  }

 private:
  t5_serial_device_t device_ = 0;
};

}  // namespace EspRomSession
