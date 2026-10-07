#pragma once
#include <HalStorage.h>
#include <mbedtls/sha256.h>

#include <string>

#include "PackageVerificationReceipt.h"

namespace RuntimePackages {
enum class ReceiptWriteResult { Failed, CloseUncertain, Complete };
// Only an exclusively owned, fully readback-verified stage may call this.
// The enclosing directory rename is the sole commit; no second receipt store.
inline ReceiptWriteResult writeVerifiedStageReceipt(const char* root, const OrdinaryPackagePlan& plan,
                                                    const uint8_t* manifest, size_t manifestBytes) {
  if (!root || !root[0] || !manifest || !manifestBytes || manifestBytes > 4096) return ReceiptWriteResult::Failed;
  uint8_t digest[32]{}, generation[16]{}, bytes[kPackageReceiptBytes]{};
  PackageVerificationReceipt receipt{};
  if (mbedtls_sha256_ret(manifest, manifestBytes, digest, 0) || !newPackageReceiptGeneration(generation) ||
      !makePackageReceipt(plan, digest, generation, receipt) || !encodePackageReceipt(receipt, bytes))
    return ReceiptWriteResult::Failed;
  const std::string path = std::string(root) + "/" + kPackageReceiptName;
  HalFile writer = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!writer.isOpen()) return ReceiptWriteResult::Failed;
  const bool written = !writer.isDirectory() && writer.write(bytes, sizeof(bytes)) == sizeof(bytes);
  if (!writer.close()) return ReceiptWriteResult::CloseUncertain;
  if (!written) return ReceiptWriteResult::Failed;
  HalFile reader = Storage.open(path.c_str(), O_RDONLY);
  if (!reader.isOpen()) return ReceiptWriteResult::Failed;
  uint8_t retained[kPackageReceiptBytes]{};
  const bool matched = !reader.isDirectory() && reader.fileSize64() == sizeof(retained) &&
                       reader.read(retained, sizeof(retained)) == static_cast<int>(sizeof(retained)) &&
                       !std::memcmp(bytes, retained, sizeof(bytes));
  if (!reader.close()) return ReceiptWriteResult::CloseUncertain;
  return matched ? ReceiptWriteResult::Complete : ReceiptWriteResult::Failed;
}
// Presence/status is deliberately independent of full content verification.
// Missing/invalid receipt data is never a trusted boot or loader shortcut.
enum class ReceiptReadResult { Missing, Invalid, CloseUncertain, Matched };
inline ReceiptReadResult readPackageReceipt(const char* root, const OrdinaryPackagePlan& plan, const uint8_t* manifest,
                                            size_t manifestBytes, PackageVerificationReceipt& receipt) {
  receipt = {};
  if (!root || !manifest || !manifestBytes || manifestBytes > 4096) return ReceiptReadResult::Invalid;
  const std::string path = std::string(root) + "/" + kPackageReceiptName;
  if (!Storage.exists(path.c_str())) return ReceiptReadResult::Missing;
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file.isOpen()) return ReceiptReadResult::Invalid;
  uint8_t bytes[kPackageReceiptBytes]{}, digest[32]{};
  const bool read = !file.isDirectory() && file.fileSize64() == sizeof(bytes) &&
                    file.read(bytes, sizeof(bytes)) == static_cast<int>(sizeof(bytes));
  if (!file.close()) return ReceiptReadResult::CloseUncertain;
  if (!read || mbedtls_sha256_ret(manifest, manifestBytes, digest, 0) ||
      !decodePackageReceipt(bytes, sizeof(bytes), receipt) || !receiptMatchesManifest(receipt, plan, digest)) {
    receipt = {};
    return ReceiptReadResult::Invalid;
  }
  return ReceiptReadResult::Matched;
}
}  // namespace RuntimePackages
