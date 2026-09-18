#include "AppPackageInstaller.h"

#include "AppManifest.h"
#include "runtime/packages/PackagePairTransaction.h"
#include "runtime/packages/PackagePreflight.h"

#include <AppManifestRules.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace RuntimePackages {
namespace {
constexpr uint64_t kMaxAppBytes = 1024u * 1024u;

bool validFilename(const char* filename) { return t5_safe_elf_name(filename); }

struct Paths {
  std::string targetElf, targetManifest, stageElf, stageManifest, backupElf, backupManifest;
  explicit Paths(const char* filename) {
    targetElf = std::string("/Apps/") + filename;
    targetManifest = targetElf.substr(0, targetElf.size() - 4) + ".json";
    stageElf = targetElf + ".part";
    stageManifest = targetManifest + ".part";
    backupElf = targetElf + ".bak";
    backupManifest = targetManifest + ".bak";
  }
  PairPaths view() const {
    return {targetElf.c_str(), targetManifest.c_str(), stageElf.c_str(),
            stageManifest.c_str(), backupElf.c_str(), backupManifest.c_str()};
  }
};

struct StorageOps {
  bool exists(const char* path) const { return Storage.exists(path); }
  bool rename(const char* from, const char* to) const { return Storage.rename(from, to); }
  bool remove(const char* path) const { return Storage.remove(path); }
};

bool verifyBytes(const char* elfPath, uint64_t declaredSize, const char* declaredSha,
                 bool hasDigest) {
  HalFile elf = Storage.open(elfPath, O_RDONLY);
  if (!elf.isOpen() || elf.isDirectory()) return false;
  const uint64_t size = elf.fileSize64();
  if (size < 52 || size > kMaxAppBytes || (hasDigest && size != declaredSize)) {
    elf.close();
    return false;
  }
  uint8_t header[20]{};
  const bool validHeader = elf.read(header, sizeof(header)) == static_cast<int>(sizeof(header)) &&
      header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' && header[3] == 'F' &&
      header[4] == 1 && header[5] == 1 && header[16] == 3 && header[17] == 0 &&
      header[18] == 94 && header[19] == 0;
  if (!validHeader) { elf.close(); return false; }
  if (!hasDigest) { elf.close(); return true; } // Legacy consistency, NOT integrity or trust.
  if (!elf.seek64(0)) { elf.close(); return false; }

  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool good = mbedtls_sha256_starts_ret(&context, 0) == 0;
  uint8_t buffer[1024];
  uint8_t digest[32]{};
  uint64_t total = 0;
  while (good && total < size) {
    const size_t want = (size - total < sizeof(buffer)) ? static_cast<size_t>(size - total) : sizeof(buffer);
    const int count = elf.read(buffer, want);
    if (count != static_cast<int>(want)) { good = false; break; }
    good = mbedtls_sha256_update_ret(&context, buffer, want) == 0;
    total += want;
    if ((total & 0x3fffu) == 0) {
      // The TWDT also watches IDLE0. Resetting loopTask's watchdog alone does
      // not let IDLE0 run during a long run of synchronous SD reads + hashing.
      esp_task_wdt_reset();
      vTaskDelay(1);
    }
  }
  if (good) good = total == size && mbedtls_sha256_finish_ret(&context, digest) == 0;
  mbedtls_sha256_free(&context);
  elf.close();
  if (!good) return false;
  constexpr char hex[] = "0123456789abcdef";
  unsigned mismatch = 0;
  for (unsigned i = 0; i < 32; ++i) {
    mismatch |= static_cast<unsigned>(declaredSha[2 * i] != hex[digest[i] >> 4]);
    mismatch |= static_cast<unsigned>(declaredSha[2 * i + 1] != hex[digest[i] & 15]);
  }
  return mismatch == 0;
}

bool verifyNamedPair(const char* elf, const char* manifest, const char* filename,
                     bool requireDigest) {
  if (!elf || !manifest || !validFilename(filename)) return false;
  t5_app_manifest_t parsed{};
  if (!readAppManifest(manifest, parsed) || std::strcmp(parsed.file_name, filename) != 0) return false;
  // readAppManifest validates JSON, duplicate keys, length, digest spelling and
  // size limits. Reparse the bounded sidecar to obtain its digest fields.
  const String raw = Storage.readFile(manifest);
  if (raw.length() == 0 || raw.length() > 2048) return false;
  JsonDocument json;
  if (deserializeJson(json, raw) || !json.is<JsonObjectConst>()) return false;
  const JsonVariantConst declaredSize = json["size_bytes"];
  const JsonVariantConst declaredSha = json["sha256"];
  const bool hasDigest = declaredSize.is<unsigned>() && declaredSha.is<const char*>();
  if (requireDigest && !hasDigest) return false;
  return verifyBytes(elf, hasDigest ? declaredSize.as<unsigned>() : 0,
                     hasDigest ? declaredSha.as<const char*>() : nullptr, hasDigest);
}
} // namespace

bool verifyAppPair(const char* elfPath, const char* manifestPath,
                   const char* expectedFilename, bool requireDigest) {
  if (!Storage.ready()) return false;
  return verifyNamedPair(elfPath, manifestPath, expectedFilename, requireDigest);
}

bool recoverAppPair(const char* filename) {
  if (!Storage.ready() || !validFilename(filename)) return false;
  Paths paths(filename);
  StorageOps ops;
  const auto verify = [filename](const char* elf, const char* manifest) {
    return verifyNamedPair(elf, manifest, filename, false);
  };
  const bool recovered = recoverPairTransaction(ops, paths.view(), verify);
  if (!recovered) LOG_ERR("APPSTORE", "Cannot safely recover package %s", filename);
  return recovered;
}

bool clearAppStage(const char* filename) {
  if (!Storage.ready() || !validFilename(filename)) return false;
  Paths paths(filename);
  StorageOps ops;
  if (!recoverAppPair(filename)) return false;
  return removeIfPresent(ops, paths.stageElf.c_str()) &&
         removeIfPresent(ops, paths.stageManifest.c_str());
}

bool publishAppPair(const char* filename, bool replacementAllowed) {
  if (!Storage.ready() || !validFilename(filename) || !replacementAllowed ||
      !safePackageEntryName(filename)) return false; // New releases use canonical FAT-safe names.
  Paths paths(filename);
  // An app may update other apps, but never its own mapped ELF. Only the
  // loader knows the active path, so this gate cannot be delegated to the app.
  const char* active = native_app_current_path();
  const std::string mappedPath = std::string("/sd") + paths.targetElf;
  if (active && std::strcmp(active, mappedPath.c_str()) == 0) {
    LOG_ERR("APPSTORE", "Refusing replacement of running application %s", filename);
    return false;
  }
  StorageOps ops;
  if (!recoverAppPair(filename) ||
      !verifyNamedPair(paths.stageElf.c_str(), paths.stageManifest.c_str(), filename, true)) {
    LOG_ERR("APPSTORE", "Refusing unverified app stage: %s", filename);
    return false;
  }
  const auto verify = [filename, &paths](const char* elf, const char* manifest) {
    // A previous installed package may be digestless, for migration only.
    const bool staged = std::strcmp(elf, paths.stageElf.c_str()) == 0;
    return verifyNamedPair(elf, manifest, filename, staged);
  };
  return publishPairTransaction(ops, paths.view(), verify, replacementAllowed);
}
} // namespace RuntimePackages
