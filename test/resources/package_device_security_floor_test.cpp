#include "runtime/packages/PackageDeviceSecurityFloor.h"
#include <nvs.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
struct Handle { std::map<std::string, Bytes> pending; };
std::map<std::string, Bytes> durable;
std::map<nvs_handle_t, Handle> handles;
nvs_handle_t nextHandle = 1;
bool failOpen = false, failWrite = false, failCommit = false;
Handle* active(nvs_handle_t handle) {
  auto it = handles.find(handle);
  return it == handles.end() ? nullptr : &it->second;
}
void resetStorage() {
  durable.clear(); handles.clear(); nextHandle = 1;
  failOpen = failWrite = failCommit = false;
}
}

// Host NVS emulation: uncommitted writes disappear on close; committed blobs
// persist between handles. Does not model flash wear or physical tamper defence.
esp_err_t nvs_open(const char* name, int mode, nvs_handle_t* out) {
  if (failOpen || !name || std::strcmp(name, "risc_pkg_floor") ||
      mode != NVS_READWRITE || !out) return -1;
  *out = nextHandle++;
  handles[*out] = {};
  return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* data, size_t* size) {
  if (!active(handle) || !key || !size) return -1;
  auto it = durable.find(key);
  if (it == durable.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (!data) { *size = it->second.size(); return ESP_OK; }
  if (*size < it->second.size()) {
    *size = it->second.size();
    return ESP_ERR_NVS_INVALID_LENGTH;
  }
  std::memcpy(data, it->second.data(), it->second.size());
  *size = it->second.size();
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key,
                       const void* data, size_t size) {
  auto* h = active(handle);
  if (failWrite || !h || !key || !data || !size || std::strlen(key) > 15) return -1;
  h->pending[key] = Bytes(static_cast<const uint8_t*>(data),
                          static_cast<const uint8_t*>(data) + size);
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
  auto* h = active(handle);
  if (failCommit || !h) return -1;
  for (const auto& record : h->pending) durable[record.first] = record.second;
  h->pending.clear();
  return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { handles.erase(handle); }

using namespace RuntimePackages;
int main() {
  resetStorage();
  auto& floor = devicePackageSecurityFloors();
  uint32_t value = 999;
  assert(floor.read(Kind::Driver, "gps-nmea", value) == FloorRead::NotEstablished && !value);
  assert(floor.advance(Kind::Driver, "gps-nmea", 4) == FloorAdvance::Advanced);
  assert(durable.size() == 1 && durable.begin()->first.size() == 15);
  assert(floor.read(Kind::Driver, "gps-nmea", value) == FloorRead::Present && value == 4);
  assert(floor.advance(Kind::Driver, "gps-nmea", 3) == FloorAdvance::Downgrade);
  assert(floor.advance(Kind::Driver, "gps-nmea", 4) == FloorAdvance::AlreadyAtOrAbove);
  failCommit = true;
  assert(floor.advance(Kind::Driver, "gps-nmea", 5) == FloorAdvance::Unavailable);
  failCommit = false;
  assert(floor.read(Kind::Driver, "gps-nmea", value) == FloorRead::Present && value == 4);
  failWrite = true;
  assert(floor.advance(Kind::Driver, "gps-nmea", 5) == FloorAdvance::Unavailable);
  failWrite = false;
  assert(floor.advance(Kind::Driver, "gps-nmea", 6) == FloorAdvance::Advanced);
  assert(floor.read(Kind::Driver, "gps-nmea", value) == FloorRead::Present && value == 6);
  assert(floor.advance(Kind::Application, "gps-nmea", 2) == FloorAdvance::Advanced);
  assert(floor.advance(Kind::Service, "gps-nmea", 1) == FloorAdvance::Advanced);
  assert(floor.advance(Kind::Provider, "gps-nmea", 3) == FloorAdvance::Advanced);
  assert(durable.size() == 4);
  assert(floor.read(Kind::Application, "gps-nmea", value) == FloorRead::Present && value == 2);
  failOpen = true;
  assert(floor.read(Kind::Driver, "gps-nmea", value) == FloorRead::Unavailable && !value);
  assert(floor.advance(Kind::Driver, "gps-nmea", 7) == FloorAdvance::Unavailable);
  failOpen = false;
  assert(floor.read(Kind::Driver, "gps-nmea", value) == FloorRead::Present && value == 6);

  // A shortened-key collision must fail closed on full identity mismatch.
  resetStorage();
  assert(floor.advance(Kind::Driver, "alpha", 7) == FloorAdvance::Advanced);
  assert(floor.advance(Kind::Driver, "beta", 9) == FloorAdvance::Advanced);
  assert(durable.size() == 2);
  auto first = durable.begin(), second = std::next(first);
  first->second = second->second;
  assert(floor.read(Kind::Driver, "alpha", value) == FloorRead::Unavailable ||
         floor.read(Kind::Driver, "beta", value) == FloorRead::Unavailable);

  resetStorage();
  assert(floor.advance(Kind::Driver, "gps-nmea", 7) == FloorAdvance::Advanced);
  durable.begin()->second[0] ^= 1;
  assert(floor.read(Kind::Driver, "gps-nmea", value) == FloorRead::Unavailable);
  assert(floor.advance(Kind::Driver, "gps-nmea", 8) == FloorAdvance::Unavailable);
  resetStorage();
  assert(floor.advance(Kind::Driver, "gps-nmea", 7) == FloorAdvance::Advanced);
  durable.begin()->second.pop_back();
  assert(floor.read(Kind::Driver, "gps-nmea", value) == FloorRead::Unavailable);
  // Reject noncanonical bytes after the NUL in the fixed-size identity field.
  resetStorage();
  assert(floor.advance(Kind::Driver, "gps-nmea", 7) == FloorAdvance::Advanced);
  assert(durable.begin()->second.size() > 32);
  durable.begin()->second[32] = 0x5a;
  assert(floor.read(Kind::Driver, "gps-nmea", value) == FloorRead::Unavailable);

  // The device adapter serializes reads and commits across concurrent writers.
  resetStorage();
  std::thread a([&] { for (uint32_t n = 1; n <= 32; ++n) (void)floor.advance(Kind::Driver, "race", n); });
  std::thread b([&] { for (uint32_t n = 1; n <= 32; ++n) (void)floor.advance(Kind::Driver, "race", n); });
  a.join(); b.join();
  assert(floor.read(Kind::Driver, "race", value) == FloorRead::Present && value == 32);
  assert(floor.advance(Kind::Driver, "race", 31) == FloorAdvance::Downgrade);
  std::puts("Device NVS security floors: commit faults, restart, collision, corruption and writer tests passed");
}
