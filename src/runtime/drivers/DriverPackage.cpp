#include "DriverPackage.h"
#include <ArduinoJson.h>
#include <NativeAppLauncher.h>
#include <Logging.h>
#include <T5DriverApi.h>
#include <T5GnssProvider.h>
#include <mbedtls/sha256.h>
#include <cstdio>
#include <cstring>

namespace {
bool matches(JsonVariantConst value, const char* expected) {
  return value.is<const char*>() && std::strcmp(value.as<const char*>(), expected) == 0;
}
bool requirement(JsonVariantConst entry, const char* id) {
  return matches(entry["capability"], id) && entry["api"].is<unsigned>() && entry["api"].as<unsigned>() == 1;
}
}
bool validateGpsDriverPackage() {
  if (native_app_register_sd_vfs() != ESP_OK) return false;
  FILE* file = std::fopen("/sd/Drivers/gps-nmea/manifest.json", "rb");
  if (!file) { LOG_ERR("DRIVER", "GPS package missing: /Drivers/gps-nmea/manifest.json"); return false; }
  char text[4097];
  size_t bytes = std::fread(text, 1, sizeof(text), file);
  const bool readError = std::ferror(file) != 0;
  std::fclose(file);
  if (readError || bytes == 0 || bytes >= sizeof(text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text, bytes)) return false;
  if (!matches(doc["type"], "driver") || !matches(doc["id"], "gps-nmea") ||
      !matches(doc["architecture"], "xtensa-esp32s3") || !matches(doc["file_name"], "driver.elf") ||
      !doc["version"].is<const char*>() || !doc["version"].as<const char*>()[0] ||
      !doc["driver_abi"].is<unsigned>() || doc["driver_abi"].as<unsigned>() != T5_DRIVER_ABI_VERSION ||
      !doc["provides"].is<JsonArray>() || doc["provides"].size() != 1 ||
      !requirement(doc["provides"][0], T5_GNSS_CAPABILITY) ||
      !doc["requires"].is<JsonArray>() || doc["requires"].size() != 3 ||
      !doc["sha256"].is<const char*>() || !doc["size_bytes"].is<unsigned>()) {
    LOG_ERR("DRIVER", "GPS manifest contract mismatch"); return false;
  }
  // This first binding supplies exactly these three primitives; unknown or
  // duplicate requirements cannot accidentally be treated as satisfied.
  unsigned requirements = 0;
  for (JsonVariantConst entry : doc["requires"].as<JsonArrayConst>()) {
    unsigned bit = requirement(entry, "kernel.serial") ? 1 : requirement(entry, "kernel.power") ? 2 :
                   requirement(entry, "kernel.clock") ? 4 : 0;
    if (!bit || (requirements & bit)) return false;
    requirements |= bit;
  }
  if (requirements != 7) return false;
  const char* expected = doc["sha256"];
  const unsigned size = doc["size_bytes"];
  if (std::strlen(expected) != 64 || size < 52 || size > 256 * 1024) return false;
  for (unsigned i = 0; i < 64; ++i) {
    if (!((expected[i] >= '0' && expected[i] <= '9') || (expected[i] >= 'a' && expected[i] <= 'f'))) return false;
  }
  file = std::fopen(GPS_DRIVER_ELF, "rb");
  if (!file) return false;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts_ret(&hash, 0) == 0;
  unsigned total = 0;
  uint8_t buffer[1024], digest[32];
  while (ok && (bytes = std::fread(buffer, 1, sizeof(buffer), file)) != 0) {
    total += bytes;
    if (total > size) { ok = false; break; }
    ok = mbedtls_sha256_update_ret(&hash, buffer, bytes) == 0;
  }
  ok = ok && !std::ferror(file) && total == size && mbedtls_sha256_finish_ret(&hash, digest) == 0;
  std::fclose(file);
  mbedtls_sha256_free(&hash);
  if (ok) {
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
      if (expected[i * 2] != hex[digest[i] >> 4] || expected[i * 2 + 1] != hex[digest[i] & 15]) { ok = false; break; }
    }
  }
  if (!ok) LOG_ERR("DRIVER", "GPS ELF integrity check failed");
  return ok;
}
