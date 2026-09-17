#include "PackageDeviceSecurityFloor.h"

#include <mbedtls/sha256.h>
#include <nvs.h>

#include <cstdio>
#include <cstring>

namespace RuntimePackages {
namespace {
constexpr char kNamespace[] = "risc_pkg_floor"; // NVS namespace <= 15 chars.
constexpr uint32_t kMagic = 0x52465031u; // RFP1
constexpr uint32_t kSchema = 1;

struct FloorRecord {
  uint32_t magic;
  uint32_t schema;
  uint32_t kind;
  char id[64];
  uint32_t floor;
};

bool validKind(Kind kind) {
  switch (kind) {
    case Kind::Application: case Kind::Driver: case Kind::Service: case Kind::Provider: return true;
    default: return false;
  }
}

// NVS keys are limited to 15 characters. Hash the full (kind, ID) tuple into
// 7 SHA-256 bytes, then compare the entire tuple in the stored record. A
// truncated-key collision must fail closed; it must never alias another floor.
bool makeKey(Kind kind, const char* id, char (&key)[16]) {
  if (!validKind(kind) || !safeId(id)) return false;
  char tuple[2 + sizeof(Identity::id)]{};
  tuple[0] = static_cast<char>('0' + static_cast<unsigned>(kind));
  tuple[1] = ':';
  const size_t idLength = std::strlen(id);
  std::memcpy(tuple + 2, id, idLength);
  uint8_t digest[32]{};
  if (mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(tuple),
                         idLength + 2, digest, 0) != 0) return false;
  constexpr char hex[] = "0123456789abcdef";
  key[0] = 'p';
  for (size_t i = 0; i < 7; ++i) {
    key[1 + i * 2] = hex[digest[i] >> 4];
    key[2 + i * 2] = hex[digest[i] & 15];
  }
  key[15] = '\0';
  return true;
}

FloorRead loadRecord(nvs_handle_t handle, const char* key, Kind kind, const char* id,
                     uint32_t& floor) {
  floor = 0;
  size_t length = 0;
  const esp_err_t measured = nvs_get_blob(handle, key, nullptr, &length);
  if (measured == ESP_ERR_NVS_NOT_FOUND) return FloorRead::NotEstablished;
  if (measured != ESP_OK || length != sizeof(FloorRecord)) return FloorRead::Unavailable;
  FloorRecord record{};
  if (nvs_get_blob(handle, key, &record, &length) != ESP_OK ||
      length != sizeof(record) || record.magic != kMagic ||
      record.schema != kSchema || record.kind != static_cast<uint32_t>(kind) ||
      record.floor == 0 || !safeId(record.id)) return FloorRead::Unavailable;
  char canonicalId[sizeof(record.id)]{};
  std::memcpy(canonicalId, id, std::strlen(id) + 1);
  if (std::memcmp(record.id, canonicalId, sizeof(record.id)) != 0)
    return FloorRead::Unavailable;
  floor = record.floor;
  return FloorRead::Present;
}
} // namespace

DevicePackageSecurityFloors& devicePackageSecurityFloors() {
  static DevicePackageSecurityFloors instance;
  return instance;
}

FloorRead DevicePackageSecurityFloors::read(Kind kind, const char* id, uint32_t& floor) {
  floor = 0;
  char key[16]{};
  if (!makeKey(kind, id, key)) return FloorRead::Unavailable;
  std::lock_guard<std::mutex> lock(mutex_);
  nvs_handle_t handle{};
  // NVS initialization and any NVS erasure policy belong to firmware startup,
  // never to this package subsystem. An inaccessible namespace fails closed.
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
    return FloorRead::Unavailable;
  const FloorRead result = loadRecord(handle, key, kind, id, floor);
  nvs_close(handle);
  return result;
}

FloorAdvance DevicePackageSecurityFloors::advance(Kind kind, const char* id,
                                                  uint32_t newFloor) {
  char key[16]{};
  if (!newFloor || !makeKey(kind, id, key)) return FloorAdvance::Unavailable;
  std::lock_guard<std::mutex> lock(mutex_);
  nvs_handle_t handle{};
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
    return FloorAdvance::Unavailable;
  uint32_t previous = 0;
  const FloorRead status = loadRecord(handle, key, kind, id, previous);
  if (status == FloorRead::Unavailable) {
    nvs_close(handle);
    return FloorAdvance::Unavailable;
  }
  if (status == FloorRead::Present && newFloor <= previous) {
    nvs_close(handle);
    return newFloor < previous ? FloorAdvance::Downgrade : FloorAdvance::AlreadyAtOrAbove;
  }
  FloorRecord record{};
  record.magic = kMagic;
  record.schema = kSchema;
  record.kind = static_cast<uint32_t>(kind);
  std::memcpy(record.id, id, std::strlen(id) + 1);
  record.floor = newFloor;
  const bool persisted = nvs_set_blob(handle, key, &record, sizeof(record)) == ESP_OK &&
                         nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return persisted ? FloorAdvance::Advanced : FloorAdvance::Unavailable;
}

} // namespace RuntimePackages
