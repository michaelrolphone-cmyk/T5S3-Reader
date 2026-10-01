#include "PackageExecutableAdmission.h"

#include <memory>
#include <mutex>
#include <new>

#include "PackageOrdinaryManifest.h"
#include "PackageOrdinaryTransaction.h"
#include "PackageVerificationReceiptSd.h"
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#include <esp_timer.h>
#else
#include <chrono>
#endif
namespace RuntimePackages {
namespace {
uint64_t now() {
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
  return static_cast<uint64_t>(esp_timer_get_time());
#else
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());
#endif
}
struct AdmittedImage {
  char root[96]{};
  uint8_t manifest[32]{}, generation[16]{}, executable[32]{};
  size_t bytes = 0;
  StorageGenerationStamp stamp{};
};
// This is a bounded verification memo, not provider discovery/registration.
// Overflow evicts the oldest slot and simply requires another cold hash.
AdmittedImage admitted[32]{};
size_t nextAdmission = 0;
std::mutex admissionMutex;
bool sameImage(const AdmittedImage& a, const AdmittedImage& b) {
  return a.stamp.matches(b.stamp) && !std::strcmp(a.root, b.root) && a.bytes == b.bytes &&
         !std::memcmp(a.manifest, b.manifest, 32) && !std::memcmp(a.generation, b.generation, 16) &&
         !std::memcmp(a.executable, b.executable, 32);
}
}  // namespace
bool packageSnapshotDigest(const uint8_t* bytes, size_t size, uint8_t (&digest)[32]) {
  std::memset(digest, 0, sizeof(digest));
  if (!bytes || !size || size > 8u * 1024u * 1024u) return false;
  const uint64_t started = now();
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool good = mbedtls_sha256_starts_ret(&hash, 0) == 0;
  for (size_t at = 0; good && at < size;) {
    if (now() - started > 10000000u) {
      good = false;
      break;
    }
    const size_t count = size - at < 512 ? size - at : 512;
    good = mbedtls_sha256_update_ret(&hash, bytes + at, count) == 0;
    at += count;
    ordinaryCooperativeYield(at, size);
  }
  if (good) good = mbedtls_sha256_finish_ret(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
  return good && now() - started <= 10000000u;
}
bool declaredPackageSnapshot(const OrdinaryPackagePlan& plan, const char* name, const uint8_t* bytes, size_t size) {
  if (!name || plan.entryCount > kMaxPackageEntries) return false;
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const auto& entry = plan.entries[i];
    if (std::strcmp(entry.name, name)) continue;
    uint8_t digest[32]{};
    return entry.sizeBytes == size && packageSnapshotDigest(bytes, size, digest) &&
           ordinaryDigestEquals(digest, entry.sha256);
  }
  return false;
}
bool admitInstalledExecutableSnapshot(const Identity& identity, const uint8_t* expectedManifestDigest,
                                      const uint8_t* expectedExecutableDigest, const uint8_t* snapshot, size_t length,
                                      const StorageGenerationStamp& sourceStamp) {
  Identity canonical{};
  OrdinaryTransactionPaths paths{};
  if (!canonicalIdentity(identity, &canonical) || resourceOnly(identity) || !expectedManifestDigest ||
      !expectedExecutableDigest || !snapshot || length < 52 || length > 8u * 1024u * 1024u ||
      !ordinaryTransactionPaths(identity.kind, identity.id, paths) || !systemPackageUseGate().pinned(paths.target) ||
      !Storage.ready())
    return false;
  const auto before = Storage.generation();
  std::unique_ptr<char[]> metadata(new (std::nothrow) char[4096]);
  std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
  if (!metadata || !plan) return false;
  HalFile file = Storage.open((std::string(paths.target) + "/" + kOrdinaryManifestName).c_str(), O_RDONLY);
  if (!file.isOpen()) return false;
  const uint64_t size = !file.isDirectory() ? file.fileSize64() : 0;
  const bool read =
      size && size <= 4096 && file.read(metadata.get(), static_cast<size_t>(size)) == static_cast<int>(size);
  const bool closed = file.close();
  if (!read || !closed || !parseOrdinaryManifest(metadata.get(), size, *plan)) return false;
  constexpr PackageRuntimePolicy policy{"xtensa-esp32s3", 2, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
  if (preflightOrdinaryPackage(*plan, policy, [](const char*) -> uint32_t { return UINT32_MAX; }) !=
      PreflightResult::ReadyForContentVerification)
    return false;
  if (!samePackage(identity, plan->identity) || std::strcmp(identity.version, plan->identity.version) ||
      std::strcmp(identity.artifact, plan->identity.artifact) || resourceOnly(plan->identity))
    return false;
  AdmittedImage candidate{};
  std::strcpy(candidate.root, paths.target);
  candidate.bytes = length;
  candidate.stamp = before;
  if (!packageSnapshotDigest(reinterpret_cast<const uint8_t*>(metadata.get()), size, candidate.manifest) ||
      std::memcmp(candidate.manifest, expectedManifestDigest, 32))
    return false;
  const OrdinaryEntry* executable = nullptr;
  for (size_t i = 0; i < plan->entryCount; ++i)
    if (plan->entries[i].executable) {
      if (executable) return false;
      executable = &plan->entries[i];
    }
  if (!executable || std::strcmp(executable->name, identity.artifact) || executable->sizeBytes != length ||
      !receiptDigest(executable->sha256, candidate.executable) ||
      std::memcmp(candidate.executable, expectedExecutableDigest, 32) ||
      !ordinaryElfHeader(snapshot, length, plan->architecture))
    return false;
  PackageVerificationReceipt receipt{};
  const auto receiptStatus =
      readPackageReceipt(paths.target, *plan, reinterpret_cast<const uint8_t*>(metadata.get()), size, receipt);
  if (receiptStatus == ReceiptReadResult::CloseUncertain) return false;
  if (receiptStatus == ReceiptReadResult::Matched) std::memcpy(candidate.generation, receipt.generation, 16);
  // Missing receipts preserve older canonical packages. Invalid receipts never
  // qualify for reuse, but cannot replace actual snapshot integrity verification.
  const bool reusable = (receiptStatus == ReceiptReadResult::Matched || receiptStatus == ReceiptReadResult::Missing) &&
                        sourceStamp.matches(before) && Storage.unchanged(before);
  bool warm = false;
  if (reusable) {
    std::lock_guard<std::mutex> lock(admissionMutex);
    for (const auto& item : admitted)
      if (sameImage(item, candidate)) {
        warm = true;
        break;
      }
  }
  if (warm) return Storage.unchanged(before);
  uint8_t digest[32]{};
  if (!packageSnapshotDigest(snapshot, length, digest) || std::memcmp(digest, candidate.executable, 32)) return false;
  if (reusable && Storage.unchanged(before)) {
    std::lock_guard<std::mutex> lock(admissionMutex);
    admitted[nextAdmission] = candidate;
    nextAdmission = (nextAdmission + 1) % 32;
  }
  // With mutable raw-storage access, the actual operation-owned snapshot is
  // still checked. No coherent proof is retained and no provider right is added.
  return true;
}
}  // namespace RuntimePackages
