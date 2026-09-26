#pragma once

#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageCatalog.h"
#include "runtime/packages/PackageOrdinarySdZipAdapter.h"
#include "runtime/packages/PackageRteZip.h"
#include "network/HttpDownloader.h"

#include <HalStorage.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace RuntimeOnlinePackages {
namespace OrdinaryZip {

// Only this exact published release may supply the archive. In particular, do
// not use releases/latest/download after catalog selection: it can drift.
inline bool archiveUrl(const RuntimePackages::CatalogPackage& package,
                       const char* releaseTag, std::string& url) {
  url.clear();
  if (!RuntimePackages::CatalogDetail::safeReleaseTag(releaseTag) ||
      !RuntimePackages::CatalogDetail::archiveName(package.archive) ||
      std::strlen(package.archive) > 95 ||
      std::strcmp(package.architecture, "xtensa-esp32s3") ||
      !RuntimePackages::CatalogDetail::lowerSha256(package.sha256) ||
      package.sizeBytes < RuntimePackages::kRteZipEocdBytes ||
      package.sizeBytes > RuntimePackages::kRteZipMaxTotalBytes + 8192u)
    return false;
  url = std::string("https://github.com/michaelrolphone-cmyk/T5S3-Reader/") +
        "releases/download/" + releaseTag + "/" + package.archive;
  return true;
}

inline bool ensureInbox() {
  for (const char* folder : {"/Packages", "/Packages/Inbox"}) {
    if (!Storage.exists(folder) && !Storage.mkdir(folder, false)) return false;
    HalFile directory = Storage.open(folder, O_RDONLY);
    const bool valid = directory.isOpen() && directory.isDirectory();
    if (directory.isOpen() && !directory.close()) return false;
    if (!valid) return false;
  }
  return true;
}

// Catalog SHA-256 is a transport-integrity pin for the selected artifact;
// each file is independently rehashed against its retained manifest before
// ordinary publication. No digest is treated as authority to activate code.
inline bool archiveMatches(const char* path,
                           const RuntimePackages::CatalogPackage& package) {
  if (!path || !RuntimePackages::CatalogDetail::lowerSha256(package.sha256))
    return false;
  HalFile file = Storage.open(path, O_RDONLY);
  if (!file.isOpen() || file.isDirectory()) {
    if (file.isOpen()) (void)file.close();
    return false;
  }
  if (file.fileSize64() != package.sizeBytes) { (void)file.close(); return false; }
  std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[1024]);
  if (!chunk) { (void)file.close(); return false; }
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool good = mbedtls_sha256_starts_ret(&hash, 0) == 0;
  uint64_t offset = 0;
  while (good && offset < package.sizeBytes) {
    const size_t count = static_cast<size_t>(std::min<uint64_t>(1024, package.sizeBytes - offset));
    good = file.read(chunk.get(), count) == static_cast<int>(count) &&
           mbedtls_sha256_update_ret(&hash, chunk.get(), count) == 0;
    offset += good ? count : 0;
    if (good && (offset % 16384u == 0 || offset == package.sizeBytes)) {
      esp_task_wdt_reset();
      vTaskDelay(1);
    }
  }
  uint8_t digest[32]{};
  if (good) good = mbedtls_sha256_finish_ret(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  const bool closed = file.close();
  if (!good || !closed) return false;
  constexpr char hex[] = "0123456789abcdef";
  unsigned mismatch = 0;
  for (unsigned i = 0; i < 32; ++i) {
    mismatch |= package.sha256[2u * i] != hex[digest[i] >> 4];
    mismatch |= package.sha256[2u * i + 1u] != hex[digest[i] & 0x0fu];
  }
  return mismatch == 0;
}

// Progress callbacks are invocation-scoped, never retained beyond download.
// Existing target ZIPs and interrupted .part files are preserved, not
// unconditionally removed. A successful verified download remains in Inbox
// for offline reinstalls. Manager callers serialize mutations and check that
// the selected version may replace any currently installed generation.
inline bool install(const RuntimePackages::CatalogPackage& package,
                    const char* releaseTag,
                    HttpDownloader::ProgressCallback progress = nullptr) {
  if (!Storage.ready()) return false;
  std::string url;
  if (!archiveUrl(package, releaseTag, url) || !ensureInbox()) return false;
  const std::string archive = std::string("/Packages/Inbox/") + package.archive;
  const std::string part = archive + ".part";
  if (archive.size() >= 120 || part.size() >= 128) return false;
  if (Storage.exists(archive.c_str())) {
    if (!archiveMatches(archive.c_str(), package)) return false;
  } else {
    // Unknown or interrupted file requires explicit recovery, never overwrite.
    if (Storage.exists(part.c_str()) ||
        HttpDownloader::downloadToFile(url, part, progress) != HttpDownloader::OK)
      return false;
    if (!archiveMatches(part.c_str(), package) ||
        !Storage.rename(part.c_str(), archive.c_str())) return false;
  }
  constexpr RuntimePackages::PackageRuntimePolicy policy{
      "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
  const auto installed = RuntimePackages::installOrdinaryFromSdZip(
      archive.c_str(), policy, RuntimePackages::installedCapabilityVersion,
      &package.identity);
  return installed.result == RuntimePackages::OrdinaryInstallResult::Installed;
}

} // namespace OrdinaryZip
} // namespace RuntimeOnlinePackages
