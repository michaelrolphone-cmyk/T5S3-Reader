#include "runtime/drivers/GpsDriverModule.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

struct Device {
  uint32_t clock = 0, baud = 0;
  bool power = false, uart = false, failPower = false, failUart = false;
  unsigned acquires = 0, releases = 0;
  std::string rx;
};
uint32_t ticks(void* ctx) { return static_cast<Device*>(ctx)->clock; }
void wait(void* ctx, uint32_t ms) { static_cast<Device*>(ctx)->clock += ms; }
bool acquire(void* ctx) {
  auto& d = *static_cast<Device*>(ctx);
  if (d.failPower) return false;
  d.power = true; ++d.acquires; return true;
}
void release(void* ctx) { auto& d = *static_cast<Device*>(ctx); if (d.power) ++d.releases; d.power = false; }
bool open(void* ctx, uint32_t baud) {
  auto& d = *static_cast<Device*>(ctx); d.baud = baud; d.uart = !d.failUart; return d.uart;
}
void close(void* ctx) { static_cast<Device*>(ctx)->uart = false; }
size_t receive(void* ctx, uint8_t* buffer, size_t capacity) {
  auto& d = *static_cast<Device*>(ctx);
  size_t n = std::min(capacity, d.rx.size());
  std::memcpy(buffer, d.rx.data(), n); d.rx.erase(0, n); return n;
}
std::string nmea(const std::string& body) {
  uint8_t checksum = 0;
  for (unsigned char c : body) checksum ^= c;
  const char* hex = "0123456789ABCDEF";
  return "$" + body + "*" + hex[checksum >> 4] + hex[checksum & 15] + "\r\n";
}
int main(int argc, char** argv) {
  assert(argc == 3);
  Device d;
  t5_kernel_io_v1 host = {1, sizeof(host), &d, ticks, wait, acquire, release, open, close, receive};
  GpsDriverModule module;
  t5_gps_state_t state{};
  assert(!module.start("/does/not/exist", &host));
  assert(!module.start(argv[2], &host)); // Wrong driver ABI must never acquire hardware.
  assert(d.acquires == 0);
  host.api_version = 2;
  assert(!module.start(argv[1], &host));
  host.api_version = 1;
  d.failPower = true;
  assert(!module.start(argv[1], &host) && !d.power);
  d.failPower = false; d.failUart = true;
  assert(!module.start(argv[1], &host) && !d.power && !d.uart && d.acquires == d.releases);
  d.failUart = false;
  assert(module.start(argv[1], &host));
  assert(d.power && d.uart && d.baud == 9600);
  assert(module.start(argv[1], &host)); // Start is idempotent, no second lease.
  assert(module.read(&state) && state.status == T5_GPS_STATUS_SEARCHING);
  d.clock += 1600;
  assert(module.read(&state) && d.baud == 38400);
  d.rx = "$GPGGA,broken*00\r\n";
  assert(module.read(&state) && !state.receiver_detected);
  const std::string gga = nmea("GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  d.rx = gga.substr(0, 15);
  assert(module.read(&state) && !state.fix_valid);
  d.rx = gga.substr(15);
  assert(module.read(&state) && state.fix_valid && state.receiver_detected && state.satellites == 8);
  assert(std::abs(state.latitude - 48.1173) < 1e-7 && std::abs(state.longitude - 11.5166666667) < 1e-7);
  assert(std::abs(state.altitude_m - 545.4f) < 0.01 && std::abs(state.hdop - 0.9f) < 0.01);
  d.clock += 5001;
  assert(module.read(&state) && !state.fix_valid && state.latitude == 0 && d.baud == 38400);
  d.rx = nmea("GPRMC,123519,A,4807.038,S,01131.000,W,10.0,84.4,230394,,,A");
  assert(module.read(&state) && state.fix_valid && state.latitude < 0 && state.longitude < 0);
  assert(std::abs(state.speed_kph - 18.52f) < 0.01 && std::abs(state.course_deg - 84.4f) < 0.01);
  d.rx = nmea("GPRMC,123520,V,,,,,,,230394,,,N");
  assert(module.read(&state) && !state.fix_valid);
  d.rx = nmea("GNGGA,123519,9967.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  assert(module.read(&state) && !state.fix_valid); // Out-of-range coordinates.
  d.rx = "$" + std::string(300, 'X') + "\r\n" + gga;
  assert(module.read(&state) && state.fix_valid); // Resync after oversized sentence.
  d.clock = UINT32_MAX - 100;
  d.rx = gga;
  assert(module.read(&state) && state.fix_valid);
  d.clock = 50;
  assert(module.read(&state) && state.fix_valid && state.age_ms == 151); // Timer wrap.
  assert(module.stop() && !d.power && !d.uart && d.acquires == d.releases);
  assert(module.read(&state) && state.status == T5_GPS_STATUS_OFF);
  assert(module.stop());
  assert(module.start(argv[1], &host)); // Reload genuinely initializes fresh ELF state.
  assert(module.read(&state) && !state.receiver_detected && !state.fix_valid);
  d.failUart = true; d.clock += 1600;
  assert(!module.read(&state) && !d.power && !d.uart);
  assert(module.state() == GpsDriverModule::State::Failed);
  assert(d.acquires == d.releases);
  assert(module.stop());
  std::cout << "GPS driver dynamic load/parser/lifecycle tests passed\n";
}
