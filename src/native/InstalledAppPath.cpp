#include "InstalledAppPath.h"

#include <AppManifestRules.h>
#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <cstring>
#include <string>
#include <utility>

#include "AppManifest.h"
#include "AppPackageInstaller.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageIdentity.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackagePreflight.h"

namespace {
constexpr size_t kMaxDirectoryEntries = 512;
constexpr uint32_t kResolveBudgetMs = 30000;
constexpr RuntimePackages::PackageRuntimePolicy kAppPolicy{
    "xtensa-esp32s3", 2, 8u * 1024u * 1024u, 16u * 1024u * 1024u};

bool validManagedCandidate(const char* id, const char* artifact,
                           t5_app_manifest_t& manifest) {
  if (!RuntimePackages::safeId(id)) return false;
  const std::string root = std::string("/Apps/") + id;
  const std::string elf = root + "/" + artifact;
  const std::string sidecar = elf.substr(0, elf.size() - 4) + ".json";
  // Cheap prefilter: avoid verifying and hashing every unrelated installed
  // package while resolving a single saved ELF basename.
  if (!Storage.exists(elf.c_str()) || !Storage.exists(sidecar.c_str())) return false;
  RuntimePackages::Identity identity{};
  if (!RuntimePackages::inspectInstalledOrdinarySdDirectory(root.c_str(), kAppPolicy,
          RuntimePackages::installedCapabilityVersion, identity) ||
      identity.kind != RuntimePackages::Kind::Application ||
      std::strcmp(identity.id, id) || std::strcmp(identity.artifact, artifact) ||
      Storage.exists((elf + ".bak").c_str()) ||
      Storage.exists((sidecar + ".bak").c_str())) return false;
  return readAppManifest(sidecar.c_str(), manifest) && manifest.compatible &&
         !std::strcmp(manifest.file_name, artifact);
}
}  // namespace

bool resolveInstalledAppPath(const char* artifact, std::string& sdPath,
                             t5_app_manifest_t* manifest) {
  sdPath.clear();
  if (manifest) *manifest = {};
  if (!Storage.ready() || !artifact || !t5_safe_elf_name(artifact)) return false;

  // Package IDs are not necessarily ELF basenames. Resolve by the verified
  // package inventory rather than synthesizing /Apps/<basename>/<basename>.elf.
  HalFile dir = Storage.open("/Apps", O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) return false;
  std::string managedPath;
  t5_app_manifest_t managedManifest{};
  bool reachedEnd = false;
  const uint32_t began = millis();
  uint32_t yielded = began, reported = began;
  size_t entries = 0;
  for (;;) {
    const uint32_t now = millis();
    if (now - began >= kResolveBudgetMs) break;
    if ((entries && (entries & 15u) == 0) || now - yielded >= 8u) {
      esp_task_wdt_reset();
      delay(1);
      yielded = millis();
    }
    HalFile::DirectoryEntry entry{};
    if (!dir.readDirectoryEntry(entry)) {
      reachedEnd = dir.getError() == 0;
      break;
    }
    if (++entries > kMaxDirectoryEntries) break;
    if (entry.isDirectory && RuntimePackages::safeId(entry.name)) {
      t5_app_manifest_t candidate{};
      if (validManagedCandidate(entry.name, artifact, candidate)) {
        // Ambiguous ELF basenames must not silently select an arbitrary
        // installed package; pin records still use this legacy basename.
        if (!managedPath.empty()) {
          dir.close();
          return false;
        }
        managedPath = std::string("/sd/Apps/") + entry.name + "/" + artifact;
        managedManifest = candidate;
      }
    }
    if (millis() - reported >= 1000u) {
      LOG_DBG("APP", "Resolve %s entries=%u", artifact, (unsigned)entries);
      reported = millis();
    }
  }
  const bool closed = dir.close();
  if (!reachedEnd || !closed || millis() - began >= kResolveBudgetMs) return false;
  if (!managedPath.empty()) {
    sdPath = std::move(managedPath);
    if (manifest) *manifest = managedManifest;
    return true;
  }

  // Preserve legacy manually installed ELF/JSON pairs without trusting a
  // stale sidecar, interrupted replacement or incompatible application.
  if (!RuntimePackages::recoverAppPair(artifact)) return false;
  const std::string elf = std::string("/Apps/") + artifact;
  const std::string sidecar = elf.substr(0, elf.size() - 4) + ".json";
  if (!Storage.exists(elf.c_str()) || !Storage.exists(sidecar.c_str()) ||
      Storage.exists((elf + ".bak").c_str()) ||
      Storage.exists((sidecar + ".bak").c_str()) ||
      !RuntimePackages::inspectInstalledAppPair(elf.c_str(), sidecar.c_str(), artifact)) return false;
  t5_app_manifest_t parsed{};
  if (!readAppManifest(sidecar.c_str(), parsed) || !parsed.compatible ||
      std::strcmp(parsed.file_name, artifact)) return false;
  sdPath = std::string("/sd") + elf;
  if (manifest) *manifest = parsed;
  return true;
}
