#pragma once

#include "AppPackageInstaller.h"
#include "NativeOnlinePackageRecovery.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "network/HttpDownloader.h"
#include <HalStorage.h>
#include <Logging.h>
#include <mbedtls/sha256.h>
#include <esp_task_wdt.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace RuntimeOnlinePackages {

// The release metadata is untrusted. Keep all downloaded bytes in a newly
// created, manager-owned inbox directory and let the ordinary package engine
// rehash, check inventory, stage and publish them. On retry reclaim ONLY a
// prior source whose exact bounded contents match this release, and a stage
// whose every byte matches the freshly verified source. Unknown paths remain.
inline bool installApplication(const char* artifact, const char* version,
                               const char* url, const std::string& sidecar,
                               uint64_t size, const char* elfDigest) {
  using namespace RuntimePackages;
  if (!artifact || !version || !url || !elfDigest ||
      !safePackageEntryName(artifact) || !safeVersion(version) ||
      !validSha256Hex(elfDigest) || size < 52 || size > 8u * 1024u * 1024u ||
      sidecar.empty() || sidecar.size() > 4096) return false;
  const size_t nameBytes = std::strlen(artifact);
  if (nameBytes <= 4 || std::strcmp(artifact + nameBytes - 4, ".elf")) return false;
  const std::string id(artifact, nameBytes - 4);
  if (!safeId(id.c_str())) return false;
  const std::string manifestName = id + ".json";
  if (!safePackageEntryName(manifestName.c_str())) return false;

  // The retained JSON buffer and up-to-16-entry plan together exceed 8 KiB.
  // Build them on the heap before calling the separately staged SD installer.
  uint8_t digest[32]{};
  if (mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(sidecar.data()),
                         sidecar.size(), digest, 0) != 0) return false;
  constexpr char alphabet[] = "0123456789abcdef";
  char jsonDigest[65]{};
  for (size_t i = 0; i < 32; ++i) {
    jsonDigest[i * 2] = alphabet[digest[i] >> 4];
    jsonDigest[i * 2 + 1] = alphabet[digest[i] & 15];
  }
  std::unique_ptr<char[]> descriptor(new (std::nothrow) char[4096]{});
  std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
  if (!descriptor || !plan) return false;
  const int count = std::snprintf(descriptor.get(), 4096,
      "{\"schema\":1,\"kind\":\"application\",\"id\":\"%s\",\"version\":\"%s\","
      "\"artifact\":\"%s\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":2,"
      "\"entries\":[{\"name\":\"%s\",\"size_bytes\":%llu,\"sha256\":\"%s\",\"executable\":true},"
      "{\"name\":\"%s\",\"size_bytes\":%llu,\"sha256\":\"%s\",\"executable\":false}],"
      "\"requires\":[]}", id.c_str(), version, artifact, artifact,
      static_cast<unsigned long long>(size), elfDigest, manifestName.c_str(),
      static_cast<unsigned long long>(sidecar.size()), jsonDigest);
  if (count <= 0 || count >= 4096 ||
      !parseOrdinaryManifest(descriptor.get(), static_cast<size_t>(count), *plan) ||
      plan->identity.kind != Kind::Application ||
      std::strcmp(plan->identity.id, id.c_str())) return false;
  constexpr PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 0,
                                        8u * 1024u * 1024u, 16u * 1024u * 1024u};
  const std::string root = "/Packages/Inbox/" + id;
  if (!Storage.ready() ||
      (!Storage.exists("/Packages") && !Storage.mkdir("/Packages", false)) ||
      (!Storage.exists("/Packages/Inbox") && !Storage.mkdir("/Packages/Inbox", false))) return false;
  if (Storage.exists(root.c_str())) {
    if (!Recovery::discardMatchingInbox(root, manifestName, sidecar, artifact,
            descriptor.get(), static_cast<size_t>(count))) {
      LOG_ERR("APPSTORE", "Existing inbox differs from this release; preserved for inspection: %s", root.c_str());
      return false;
    }
    LOG_INF("APPSTORE", "Recovered matching interrupted download for %s", id.c_str());
  }
  if (!Storage.mkdir(root.c_str(), false)) return false;

  auto writeExclusive = [](const std::string& filename,
                           const char* contents, size_t bytes) -> bool {
    HalFile file = Storage.open(filename.c_str(), O_WRONLY | O_CREAT | O_EXCL);
    if (!file.isOpen() || file.isDirectory()) {
      if (file.isOpen()) (void)file.close();
      return false;
    }
    const bool written = file.write(reinterpret_cast<const uint8_t*>(contents), bytes) == bytes;
    return file.close() && written;
  };
  const std::string jsonPath = root + "/" + manifestName;
  if (!writeExclusive(jsonPath, sidecar.data(), sidecar.size())) return false;
  const std::string elfPath = root + "/" + artifact;
  const std::string elfStage = elfPath + ".part";
  if (HttpDownloader::downloadToFile(url, elfStage,
          [](size_t, size_t) { esp_task_wdt_reset(); }) != HttpDownloader::OK ||
      Storage.exists(elfPath.c_str()) ||
      !Storage.rename(elfStage.c_str(), elfPath.c_str()) ||
      !verifyAppPair(elfPath.c_str(), jsonPath.c_str(), artifact, true)) return false;
  if (!writeExclusive(root + "/.package.json", descriptor.get(), static_cast<size_t>(count)))
    return false;
  // The source has passed the release digest and exact sidecar checks. Before
  // re-staging, recover only a previous stage whose contents match this source
  // byte for byte (or its interrupted prefix). Never remove an unknown stage.
  if (!Recovery::discardMatchingStage(root, *plan, policy, installedCapabilityVersion)) {
    LOG_ERR("APPSTORE", "Unrecognized or mismatched package stage preserved for inspection: %s", id.c_str());
    return false;
  }
  const auto installed = installOrdinaryFromSd(root.c_str(), policy,
                                                installedCapabilityVersion);
  if (installed.result != OrdinaryInstallResult::Installed) {
    LOG_ERR("APPSTORE", "Canonical install rejected for %s: result=%u stage=%u transaction=%u",
            id.c_str(), static_cast<unsigned>(installed.result),
            static_cast<unsigned>(installed.staging), static_cast<unsigned>(installed.transaction));
    return false;
  }

  // Source directory belongs to this invocation; clean up only exact files
  // after the managed target has been committed. Cleanup failure must not
  // incorrectly report that a completed publication failed.
  (void)Storage.remove((root + "/.package.json").c_str());
  (void)Storage.remove(elfPath.c_str());
  (void)Storage.remove(jsonPath.c_str());
  (void)Storage.rmdir(root.c_str());
  return true;
}
} // namespace RuntimeOnlinePackages
