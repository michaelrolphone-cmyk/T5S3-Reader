#include "runtime/drivers/InstalledSerialSession.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
uint64_t openedDevice = 0;
uint64_t nextToken = 500;
bool failConfigure = false;
bool failClose = false;
unsigned closes = 0;
bool lastDtr = false, lastRts = false;
uint32_t lastBaud = 0;

uint64_t openPort(uint64_t device) {
  openedDevice = device;
  return device ? ++nextToken : 0;
}
bool configurePort(uint64_t token, uint32_t baud, uint8_t bits,
                   uint8_t parity, uint8_t stops) {
  assert(token && bits >= 5 && bits <= 8 && parity <= 4 &&
         (stops == 1 || stops == 2));
  lastBaud = baud;
  return !failConfigure;
}
bool controlPort(uint64_t token, bool dtr, bool rts) {
  assert(token);
  lastDtr = dtr; lastRts = rts;
  return true;
}
int32_t readPort(uint64_t token, uint8_t* dst, size_t cap, uint32_t) {
  assert(token && dst && cap >= 2);
  dst[0] = 'o'; dst[1] = 'k';
  return 2;
}
int32_t writePort(uint64_t token, const uint8_t* src, size_t len, uint32_t) {
  assert(token && src && len);
  return len > 2 ? 2 : static_cast<int32_t>(len);
}
bool closePort(uint64_t token) {
  assert(token);
  ++closes;
  return !failClose;
}

const risc_serial_port_api_v1 api = {
    RISC_SERIAL_PORT_API_V1, sizeof(risc_serial_port_api_v1),
    openPort, configurePort, controlPort, readPort, writePort, closePort};
}

int main() {
  using RuntimeInstalledProviders::InstalledSerialSession;
  InstalledSerialSession session;
  t5_serial_config_t config{115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};

  assert(!session.bind(nullptr, 1, 1));
  assert(session.bind(&api, 44, 9001));
  assert(session.bound() && session.providerDevice() == 44 &&
         session.providerGeneration() == 9001);
  assert(session.open(config) && session.started() && openedDevice == 44);
  assert(session.control(true, false) && session.dtr() && !session.rts());
  config.baud_rate = 230400;
  assert(session.configure(config) && session.config().baud_rate == 230400 &&
         lastBaud == 230400);

  uint8_t bytes[8]{};
  uint32_t count = 0;
  assert(session.read(bytes, sizeof(bytes), &count) == T5_STREAM_OK &&
         count == 2 && !std::memcmp(bytes, "ok", 2));
  assert(session.write(reinterpret_cast<const uint8_t*>("abcd"), 4, &count) ==
         T5_STREAM_OK && count == 2);

  // Failed close retains the exact provider/session generation for retry.
  failClose = true;
  const uint64_t retained = session.token();
  assert(!session.closeChecked() && session.token() == retained &&
         session.providerDevice() == 44 && session.providerGeneration() == 9001);
  assert(!session.unbindChecked() && session.bound() && session.token() == retained);
  failClose = false;
  assert(session.unbindChecked() && !session.bound() && !session.token());
  assert(closes == 3); // closeChecked + unbind retry + final successful unbind

  // Configure failure after open must also retain an uncertain close token.
  assert(session.bind(&api, 55, 9002));
  failConfigure = true;
  failClose = true;
  assert(!session.open(config) && !session.started() && session.token());
  const uint64_t failedOpenToken = session.token();
  failConfigure = false;
  assert(session.read(bytes, sizeof(bytes), &count) == T5_STREAM_CLOSED && !count);
  assert(!session.closeChecked() && session.token() == failedOpenToken);
  failClose = false;
  assert(session.closeChecked() && !session.token());
  assert(session.unbindChecked() && !session.bound());

  std::puts("Installed serial session exact-generation lifecycle: PASS");
}
