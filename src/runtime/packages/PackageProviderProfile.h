#pragma once

#include "PackageArchiveVerification.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimePackages {

// RISC-PKG v1 provider profile (ordinary, signed resource entries; no new
// archive format or second signature). Both resources MUST be in the same
// authenticated archive as the executable:
//   provider-abi.v1: "os-cpu-abi=1\nprovides=CAPABILITY\napi=DECIMAL\n"
//   privileged-imports.v1: sorted, unique ASCII ELF symbols, one per LF line.
// The package signer binds the SHA-256 of ALL THREE entry payloads and the
// package identity, dependency requirements, version, architecture and key ID
// through the existing canonical signed manifest. The runtime separately
// enforces exact ELF symbol-table equality before mapping. No profile grants
// hardware access, execution-context permissions, or activation by itself.
constexpr const char* kProviderAbiEntryV1 = "provider-abi.v1";
constexpr const char* kProviderImportsEntryV1 = "privileged-imports.v1";
constexpr size_t kProviderImportLimitV1 = 128;
constexpr size_t kProviderImportNameLimitV1 = 127;
constexpr size_t kProviderProfileTextLimitV1 = 160;

// Caller allocates this (~17 KiB) in manager-owned long-lived RAM, NOT on a
// small FreeRTOS task stack. Strings, requirement arrays, import names,
// digests and identity are COPIED; they never borrow SD/manifest pointers.
// Its fields are data, not an unforgeable authority token: only the privileged
// manager may call the eventual executor/graph registration with these values.
struct ProviderProfileReceipt {
  Identity identity{};
  char architecture[32]{};
  char provides[64]{};
  uint32_t capabilityApi = 0;
  uint32_t requiredOsCpuAbi = 0;
  uint32_t minRuntimeApi = 0;
  uint32_t securityVersion = 0;
  uint32_t signerKeyId = 0;
  uint64_t executableLength = 0;
  uint8_t executableSha256[32]{};
  uint8_t signedPrefixSha256[32]{};
  uint16_t requirementCount = 0;
  ArchiveRequirement requirements[kMaxPackageRequirements]{};
  uint16_t importCount = 0;
  char imports[kProviderImportLimitV1][kProviderImportNameLimitV1 + 1]{};

  void importPointers(const char* out[kProviderImportLimitV1]) const {
    if (!out) return;
    for (size_t i = 0; i < importCount; ++i) out[i] = imports[i];
  }
};

enum class ProviderProfileResult : uint8_t {
  VerifiedSignedProfile, InvalidInput, UnauthenticatedArchive,
  DifferentPackage, PolicyRejected, InvalidProfile, ImportListRejected,
  EntryReadFailure, EntryDigestMismatch, SourceChanged
};

namespace ProviderProfileDetail {
inline bool sameDigest(const uint8_t* left, const uint8_t* right) {
  uint8_t different = 0;
  for (size_t i = 0; i < 32; ++i)
    different |= static_cast<uint8_t>(left[i] ^ right[i]);
  return different == 0;
}
inline int hexDigit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}
inline bool decodeDigest(const char hex[65], uint8_t bytes[32]) {
  for (size_t i = 0; i < 32; ++i) {
    const int high = hexDigit(hex[i * 2]), low = hexDigit(hex[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    bytes[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return hex[64] == '\0';
}
inline bool digestMatches(const char hex[65], const uint8_t digest[32]) {
  uint8_t expected[32]{};
  return decodeDigest(hex, expected) && sameDigest(expected, digest);
}
inline bool importCharacter(uint8_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}
inline bool parseProfile(const uint8_t* data, size_t length,
                         ProviderProfileReceipt& result) {
  constexpr char prefix[] = "os-cpu-abi=1\nprovides=";
  constexpr char apiPrefix[] = "\napi=";
  if (!data || length <= sizeof(prefix) - 1 + sizeof(apiPrefix) - 1 + 2 ||
      length > kProviderProfileTextLimitV1 ||
      std::memcmp(data, prefix, sizeof(prefix) - 1)) return false;
  size_t at = sizeof(prefix) - 1, capabilityLength = 0;
  while (at < length && data[at] != '\n') {
    if (++capabilityLength >= sizeof(result.provides)) return false;
    const uint8_t c = data[at++];
    if (c < 0x21 || c > 0x7e) return false;
    result.provides[capabilityLength - 1] = static_cast<char>(c);
  }
  result.provides[capabilityLength] = '\0';
  if (!safePackageCapability(result.provides) ||
      at + sizeof(apiPrefix) - 1 >= length ||
      std::memcmp(data + at, apiPrefix, sizeof(apiPrefix) - 1)) return false;
  at += sizeof(apiPrefix) - 1;
  if (data[at] == '0' || data[length - 1] != '\n') return false;
  uint32_t number = 0;
  const size_t digits = length - at - 1;
  if (!digits || digits > 10) return false;
  for (; at + 1 < length; ++at) {
    const uint8_t c = data[at];
    if (c < '0' || c > '9' ||
        number > (UINT32_MAX - static_cast<uint32_t>(c - '0')) / 10u)
      return false;
    number = number * 10u + static_cast<uint32_t>(c - '0');
  }
  if (!number) return false;
  result.requiredOsCpuAbi = 1;
  result.capabilityApi = number;
  return true;
}
} // namespace ProviderProfileDetail

// ReadAt: exact byte read from ONE already-opened signed .risc archive.
// Hash/Signer: same firmware-owned cryptographic policy used by
// verifyPackageArchive; Resolver/Floor are privileged runtime checks.
// expectedSignedPrefixDigest MUST come from earlier authenticated intake,
// NEVER from the archive being read in this invocation. Always clear outputs
// on failure. This constructs authenticated METADATA, not an executable grant.
template <typename ReadAt, typename Hash, typename Signer,
          typename Resolver, typename Floor>
ProviderProfileResult verifySignedProviderProfile(ReadAt readAt, uint64_t length,
    const uint8_t expectedSignedPrefixDigest[32], Hash& hash, Signer& signer,
    Resolver resolver, Floor floor, const PackageRuntimePolicy& policy,
    PackageArchive& archive, PackageVerificationWorkspace& workspace,
    ProviderProfileReceipt& receipt, PackageArchiveLimits limits = {}) {
  archive = {};
  receipt = {};
  if (!expectedSignedPrefixDigest || !policy.architecture || !policy.runtimeApi ||
      length < kPackageHeaderBytes)
    return ProviderProfileResult::InvalidInput;
  auto reject = [&archive, &receipt](ProviderProfileResult reason) {
    archive = {};
    receipt = {};
    return reason;
  };
  if (verifyPackageArchive(readAt, length, archive, workspace, hash, signer,
                           limits) != PackageVerifyResult::AuthenticatedContent)
    return reject(ProviderProfileResult::UnauthenticatedArchive);
  uint8_t digest[32]{};
  const size_t prefixLength = static_cast<size_t>(archive.signatureOffset);
  if (!hash.start() || !hash.update(workspace.signedPrefix, prefixLength) ||
      !hash.finish(digest) || !ProviderProfileDetail::sameDigest(
          digest, expectedSignedPrefixDigest))
    return reject(ProviderProfileResult::DifferentPackage);
  if ((archive.identity.kind != Kind::Driver &&
       archive.identity.kind != Kind::Provider) ||
      preflightArchive(archive, policy, resolver) !=
          PreflightResult::ReadyForContentVerification || !floor(archive))
    return reject(ProviderProfileResult::PolicyRejected);
  const ArchiveEntry *elf = nullptr, *profile = nullptr, *imports = nullptr;
  for (size_t i = 0; i < archive.entryCount; ++i) {
    const ArchiveEntry& entry = archive.entries[i];
    if (entry.executable) elf = &entry;
    if (std::strcmp(entry.name, kProviderAbiEntryV1) == 0) profile = &entry;
    if (std::strcmp(entry.name, kProviderImportsEntryV1) == 0) imports = &entry;
  }
  if (!elf || !profile || !imports || profile->executable || imports->executable ||
      std::strcmp(elf->name, archive.identity.artifact) ||
      profile->sizeBytes > kProviderProfileTextLimitV1 ||
      !profile->sizeBytes || !imports->sizeBytes ||
      imports->sizeBytes > kProviderImportLimitV1 *
                              (kProviderImportNameLimitV1 + 1u))
    return reject(ProviderProfileResult::InvalidProfile);
  uint8_t profileText[kProviderProfileTextLimitV1]{};
  if (!readAt(profile->offset, profileText,
              static_cast<size_t>(profile->sizeBytes)))
    return reject(ProviderProfileResult::EntryReadFailure);
  if (!hash.start() || !hash.update(profileText,
                                   static_cast<size_t>(profile->sizeBytes)) ||
      !hash.finish(digest) ||
      !ProviderProfileDetail::digestMatches(profile->sha256, digest))
    return reject(ProviderProfileResult::EntryDigestMismatch);
  if (!ProviderProfileDetail::parseProfile(profileText,
                    static_cast<size_t>(profile->sizeBytes), receipt))
    return reject(ProviderProfileResult::InvalidProfile);

  // Stream the import resource with <=512-byte reads, parsing directly into
  // receipt-owned bounded storage. Never trust self-declared symbol names or
  // retain a caller/SD pointer. The private ELF relocator checks actual symbol
  // table equality independently against these same names at activation.
  if (!hash.start()) return reject(ProviderProfileResult::ImportListRejected);
  char current[kProviderImportNameLimitV1 + 1]{};
  size_t nameLength = 0;
  uint64_t at = 0;
  while (at < imports->sizeBytes) {
    const size_t count = imports->sizeBytes - at < sizeof(workspace.io) ?
        static_cast<size_t>(imports->sizeBytes - at) : sizeof(workspace.io);
    if (!readAt(imports->offset + at, workspace.io, count))
      return reject(ProviderProfileResult::EntryReadFailure);
    if (!hash.update(workspace.io, count))
      return reject(ProviderProfileResult::EntryDigestMismatch);
    for (size_t i = 0; i < count; ++i) {
      const uint8_t c = workspace.io[i];
      if (c == '\n') {
        if (!nameLength || receipt.importCount >= kProviderImportLimitV1)
          return reject(ProviderProfileResult::ImportListRejected);
        current[nameLength] = '\0';
        if (receipt.importCount &&
            std::strcmp(receipt.imports[receipt.importCount - 1], current) >= 0)
          return reject(ProviderProfileResult::ImportListRejected);
        std::memcpy(receipt.imports[receipt.importCount++], current,
                    nameLength + 1);
        nameLength = 0;
      } else {
        if (!ProviderProfileDetail::importCharacter(c) ||
            nameLength >= kProviderImportNameLimitV1)
          return reject(ProviderProfileResult::ImportListRejected);
        current[nameLength++] = static_cast<char>(c);
      }
    }
    at += count;
  }
  if (nameLength || !receipt.importCount || !hash.finish(digest) ||
      !ProviderProfileDetail::digestMatches(imports->sha256, digest))
    return reject(ProviderProfileResult::ImportListRejected);

  receipt.identity = archive.identity;
  std::memcpy(receipt.architecture, archive.architecture,
              sizeof(receipt.architecture));
  receipt.minRuntimeApi = archive.minRuntimeApi;
  receipt.securityVersion = archive.securityVersion;
  receipt.signerKeyId = archive.keyId;
  receipt.executableLength = elf->sizeBytes;
  if (!ProviderProfileDetail::decodeDigest(elf->sha256,
                                            receipt.executableSha256))
    return reject(ProviderProfileResult::InvalidProfile);
  receipt.requirementCount = archive.requirementCount;
  for (size_t i = 0; i < archive.requirementCount; ++i)
    receipt.requirements[i] = archive.requirements[i];
  std::memcpy(receipt.signedPrefixSha256, expectedSignedPrefixDigest, 32);

  // A removable SD source can change during any callback. Verify everything
  // a second time and require the identical ORIGINAL signed prefix. Later
  // activation must still hash a private snapshot against executableSha256.
  if (verifyPackageArchive(readAt, length, archive, workspace, hash, signer,
                           limits) != PackageVerifyResult::AuthenticatedContent ||
      !hash.start() || !hash.update(workspace.signedPrefix,
                                    static_cast<size_t>(archive.signatureOffset)) ||
      !hash.finish(digest) ||
      !ProviderProfileDetail::sameDigest(digest,
                                         receipt.signedPrefixSha256) ||
      preflightArchive(archive, policy, resolver) !=
          PreflightResult::ReadyForContentVerification || !floor(archive))
    return reject(ProviderProfileResult::SourceChanged);
  return ProviderProfileResult::VerifiedSignedProfile;
}

} // namespace RuntimePackages
