#include "runtime/programmer/EspRomSession.h"
#include <cassert>
#include <cstdio>

using EspRomSession::Observation;
using EspRomSession::Watch;

static t5_serial_port_state_t state(uint8_t status, uint8_t connected,
                                    t5_serial_device_t device, int32_t error = 0) {
  t5_serial_port_state_t result{};
  result.status = status;
  result.connected = connected;
  result.device = device;
  result.last_error = error;
  return result;
}

int main() {
  // A host which has never claimed a device must be allowed to start VBUS and
  // wait for enumeration, not be misidentified as an unplugged target.
  Watch absent;
  auto waiting = state(T5_SERIAL_STATUS_OFF, 0, 0);
  assert(absent.observe(T5_SERIAL_OK, &waiting) == Observation::Waiting);
  waiting.status = T5_SERIAL_STATUS_WAITING;
  assert(absent.observe(T5_SERIAL_OK, &waiting) == Observation::Waiting);
  assert(absent.device() == 0);

  // Bind identity at CONFIGURING: losing the USB device before READY is loss,
  // even when the host has already reset its state to WAITING.
  Watch preReady;
  auto configuring = state(T5_SERIAL_STATUS_CONFIGURING, 1, 101);
  assert(preReady.observe(T5_SERIAL_OK, &configuring) == Observation::Waiting);
  assert(preReady.device() == 101);
  auto detached = state(T5_SERIAL_STATUS_WAITING, 0, 0, T5_SERIAL_DISCONNECTED);
  assert(preReady.observe(T5_SERIAL_OK, &detached) == Observation::Lost);

  // An identical device which reappears between samples has a different
  // generation-safe ID; the old programming operation cannot follow it.
  Watch replug;
  assert(replug.observe(T5_SERIAL_OK, &configuring) == Observation::Waiting);
  auto replacement = state(T5_SERIAL_STATUS_READY, 1, 103);
  assert(replug.observe(T5_SERIAL_OK, &replacement) == Observation::Lost);

  // READY latches identity too; a healthy session is still accepted.
  Watch ready;
  auto original = state(T5_SERIAL_STATUS_READY, 1, 101);
  assert(ready.observe(T5_SERIAL_OK, &original) == Observation::Ready);
  assert(ready.observe(T5_SERIAL_OK, &original) == Observation::Ready);
  assert(ready.observe(T5_SERIAL_OK, &detached) == Observation::Lost);

  // Explicit provider revocation must be distinguished from generic IO,
  // including when it happens before the first status snapshot succeeds.
  Watch direct;
  assert(direct.observe(T5_SERIAL_DISCONNECTED, nullptr) == Observation::Lost);
  assert(direct.observe(T5_SERIAL_CLOSED, nullptr) == Observation::Lost);
  assert(direct.observe(T5_SERIAL_IO, nullptr) == Observation::Error);
  auto bad = state(T5_SERIAL_STATUS_ERROR, 0, 0, T5_SERIAL_IO);
  assert(direct.observe(T5_SERIAL_OK, &bad) == Observation::Error);
  assert(Watch::lostControl(T5_SERIAL_DISCONNECTED));
  assert(Watch::lostControl(T5_SERIAL_CLOSED));
  assert(!Watch::lostControl(T5_SERIAL_IO));
  assert(Watch::lostStream(T5_STREAM_DISCONNECTED));
  assert(Watch::lostStream(T5_STREAM_CLOSED));
  assert(Watch::lostStream(T5_STREAM_EOF));
  assert(!Watch::lostStream(T5_STREAM_IO));
  assert(!Watch::lostStream(T5_STREAM_AGAIN));
  std::puts("ESP ROM semantic target-loss tests passed");
}
