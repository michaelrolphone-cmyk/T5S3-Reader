#pragma once
#include <cstdint>
#include <cstring>

#include "PackageOrdinaryStage.h"

namespace RuntimePackages {
// Manager-owned commit metadata, never a signature or boot-time trust grant.
// The exact manifest digest binds architecture, runtime policy, dependencies
// and every declared leaf. A fresh generation distinguishes installation acts;
// observed RAM storage epochs are intentionally NOT serialized here.
constexpr char kPackageReceiptName[] = ".package.receipt";
constexpr size_t kPackageReceiptBytes = 328;
struct PackageVerificationReceipt {
  Identity identity{};
  uint8_t generation[16]{};
  uint8_t manifestSha256[32]{};
  uint64_t executableBytes = 0;
  uint8_t executableSha256[32]{};
};
inline bool receiptAny(const uint8_t* bytes, size_t count) {
  uint8_t value = 0;
  for (size_t i = 0; i < count; ++i) value |= bytes[i];
  return value != 0;
}
inline bool receiptDigest(const char* text, uint8_t (&out)[32]) {
  if (!text) return false;
  for (size_t i = 0; i < 32; ++i) {
    unsigned value = 0;
    for (size_t j = 0; j < 2; ++j) {
      const char c = text[i * 2 + j];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
      value = value * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
    }
    out[i] = static_cast<uint8_t>(value);
  }
  return text[64] == 0;
}
inline bool validPackageReceipt(const PackageVerificationReceipt& receipt) {
  Identity canonical{};
  if (!canonicalIdentity(receipt.identity, &canonical) || !receiptAny(receipt.generation, sizeof(receipt.generation)))
    return false;
  if (resourceOnly(receipt.identity)) return !receipt.executableBytes && !receiptAny(receipt.executableSha256, 32);
  return receipt.executableBytes >= 52 && receipt.executableBytes <= 8u * 1024u * 1024u;
}
inline bool makePackageReceipt(const OrdinaryPackagePlan& plan, const uint8_t (&manifestDigest)[32],
                               const uint8_t (&generation)[16], PackageVerificationReceipt& out) {
  out = {};
  if (!canonicalIdentity(plan.identity, &out.identity) || !plan.entryCount || plan.entryCount > kMaxPackageEntries)
    return false;
  std::memcpy(out.generation, generation, 16);
  std::memcpy(out.manifestSha256, manifestDigest, 32);
  size_t executables = 0;
  for (size_t i = 0; i < plan.entryCount; ++i) {
    const auto& entry = plan.entries[i];
    if (!entry.executable) continue;
    if (++executables != 1 || std::strcmp(entry.name, plan.identity.artifact) ||
        !receiptDigest(entry.sha256, out.executableSha256))
      return false;
    out.executableBytes = entry.sizeBytes;
  }
  return executables == (resourceOnly(plan.identity) ? 0u : 1u) && validPackageReceipt(out);
}
inline bool encodePackageReceipt(const PackageVerificationReceipt& receipt, uint8_t (&bytes)[kPackageReceiptBytes]) {
  std::memset(bytes, 0, sizeof(bytes));
  if (!validPackageReceipt(receipt)) return false;
  const uint8_t magic[8] = {'R', 'T', 'E', 'V', 'R', 1, 0, 0};
  std::memcpy(bytes, magic, 8);
  bytes[8] = static_cast<uint8_t>(receipt.identity.kind);
  bytes[9] = static_cast<uint8_t>(receipt.identity.payload);
  std::memcpy(bytes + 16, receipt.generation, 16);
  std::memcpy(bytes + 32, receipt.identity.id, std::strlen(receipt.identity.id));
  std::memcpy(bytes + 96, receipt.identity.version, std::strlen(receipt.identity.version));
  std::memcpy(bytes + 128, receipt.identity.artifact, std::strlen(receipt.identity.artifact));
  std::memcpy(bytes + 256, receipt.manifestSha256, 32);
  for (size_t i = 0; i < 8; ++i) bytes[288 + i] = static_cast<uint8_t>(receipt.executableBytes >> (8 * i));
  std::memcpy(bytes + 296, receipt.executableSha256, 32);
  return true;
}
inline bool decodePackageReceipt(const uint8_t* bytes, size_t size, PackageVerificationReceipt& out) {
  out = {};
  const uint8_t magic[8] = {'R', 'T', 'E', 'V', 'R', 1, 0, 0};
  if (!bytes || size != kPackageReceiptBytes || std::memcmp(bytes, magic, 8) ||
      bytes[8] > static_cast<uint8_t>(Kind::Provider) || bytes[9] > static_cast<uint8_t>(Payload::Resources) ||
      receiptAny(bytes + 10, 6))
    return false;
  out.identity.kind = static_cast<Kind>(bytes[8]);
  out.identity.payload = static_cast<Payload>(bytes[9]);
  std::memcpy(out.generation, bytes + 16, 16);
  const auto text = [](const uint8_t* source, char* dest, size_t capacity) {
    const auto* end = static_cast<const uint8_t*>(std::memchr(source, 0, capacity));
    if (!end || receiptAny(end, capacity - static_cast<size_t>(end - source))) return false;
    std::memcpy(dest, source, capacity);
    return true;
  };
  if (!text(bytes + 32, out.identity.id, sizeof(out.identity.id)) ||
      !text(bytes + 96, out.identity.version, sizeof(out.identity.version)) ||
      !text(bytes + 128, out.identity.artifact, sizeof(out.identity.artifact)))
    return false;
  std::memcpy(out.manifestSha256, bytes + 256, 32);
  for (size_t i = 0; i < 8; ++i) out.executableBytes |= uint64_t(bytes[288 + i]) << (8 * i);
  std::memcpy(out.executableSha256, bytes + 296, 32);
  return validPackageReceipt(out);
}
inline bool receiptMatchesManifest(const PackageVerificationReceipt& receipt, const OrdinaryPackagePlan& plan,
                                   const uint8_t (&manifestDigest)[32]) {
  PackageVerificationReceipt expected{};
  uint8_t actual[kPackageReceiptBytes]{}, encoded[kPackageReceiptBytes]{};
  return makePackageReceipt(plan, manifestDigest, receipt.generation, expected) &&
         encodePackageReceipt(receipt, actual) && encodePackageReceipt(expected, encoded) &&
         !std::memcmp(actual, encoded, sizeof(actual));
}
// Production implementation supplies a fresh opaque marker, not an auth key.
bool newPackageReceiptGeneration(uint8_t (&out)[16]);
}  // namespace RuntimePackages
