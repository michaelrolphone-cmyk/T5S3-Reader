#pragma once

#include "PackageArchive.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimePackages {

// Caller-owned bounded workspace: allocate in runtime-managed memory, not on a
// small FreeRTOS task stack. No entry or ELF is buffered in its entirety.
struct PackageVerificationWorkspace {
  uint8_t signedPrefix[kPackageHeaderBytes + kPackageManifestLimit]{};
  uint8_t manifestScratch[kPackageManifestLimit]{};
  uint8_t io[512]{};
};

enum class PackageVerifyResult : uint8_t {
  AuthenticatedContent, InvalidInput, ReadFailure, InvalidArchive,
  DigestFailure, SignatureRejected, PayloadCorrupt, HiddenExecutable
};

// Hash API: bool start(), bool update(const uint8_t*, size_t),
// bool finish(uint8_t digest[32]). Signature API:
// bool verify(uint32_t keyId, const uint8_t digest[32], const uint8_t signature[64]).
// The verifier MUST resolve keyId only in firmware/device-trusted material,
// apply key revocation/scope policy, and perform real P-256 signature checking.
// An absent verifier or empty key allowlist must reject every archive.
// This verifies content read at inspection time, NOT an SD-to-load pin.
template <typename ReadAt, typename Hash, typename VerifySignature>
PackageVerifyResult verifyPackageArchive(ReadAt readAt, uint64_t fileLength,
                                         PackageArchive& out,
                                         PackageVerificationWorkspace& workspace,
                                         Hash& hash, VerifySignature& verifySignature,
                                         PackageArchiveLimits limits = {}) {
  out = {};
  if (fileLength < kPackageHeaderBytes) return PackageVerifyResult::InvalidInput;
  uint8_t* const prefix = workspace.signedPrefix;
  if (!readAt(0, prefix, kPackageHeaderBytes)) return PackageVerifyResult::ReadFailure;
  if (std::memcmp(prefix, "RISCPKG1", 8) != 0) return PackageVerifyResult::InvalidArchive;
  const uint32_t manifestLength = ArchiveDetail::le32(prefix + 12);
  if (manifestLength < 16 || manifestLength > kPackageManifestLimit)
    return PackageVerifyResult::InvalidArchive;
  const size_t prefixLength = kPackageHeaderBytes + manifestLength;
  if (!readAt(kPackageHeaderBytes, prefix + kPackageHeaderBytes, manifestLength))
    return PackageVerifyResult::ReadFailure;
  // Decode exactly the immutable bytes that are about to be signature-checked,
  // not another mutable SD read of manifest metadata.
  auto immutablePrefix = [prefix, prefixLength](uint64_t offset, uint8_t* dest, size_t count) {
    if (offset > prefixLength || count > prefixLength - static_cast<size_t>(offset)) return false;
    std::memcpy(dest, prefix + static_cast<size_t>(offset), count);
    return true;
  };
  if (decodePackageArchive(immutablePrefix, fileLength, out, workspace.manifestScratch,
                           sizeof(workspace.manifestScratch), limits) !=
      ArchiveResult::ReadyForAuthentication) return PackageVerifyResult::InvalidArchive;
  if (out.signatureOffset != prefixLength ||
      !hash.start() || !hash.update(prefix, prefixLength)) return PackageVerifyResult::DigestFailure;
  uint8_t digest[32]{};
  if (!hash.finish(digest)) return PackageVerifyResult::DigestFailure;
  uint8_t signature[kPackageSignatureBytes]{};
  if (!readAt(out.signatureOffset, signature, sizeof(signature)))
    return PackageVerifyResult::ReadFailure;
  if (!verifySignature(out.keyId, digest, signature))
    return PackageVerifyResult::SignatureRejected;

  const uint16_t expectedMachine =
      std::strcmp(out.architecture, "xtensa-esp32s3") == 0 ? 94u :
      std::strcmp(out.architecture, "riscv32") == 0 ? 243u : 0u;
  if (!expectedMachine) return PackageVerifyResult::InvalidArchive;
  constexpr char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < out.entryCount; ++i) {
    const ArchiveEntry& entry = out.entries[i];
    if (!hash.start()) return PackageVerifyResult::DigestFailure;
    uint64_t remaining = entry.sizeBytes;
    uint64_t offset = entry.offset;
    bool firstChunk = true;
    while (remaining) {
      const size_t count = remaining < sizeof(workspace.io) ?
          static_cast<size_t>(remaining) : sizeof(workspace.io);
      if (!readAt(offset, workspace.io, count)) return PackageVerifyResult::ReadFailure;
      if (firstChunk) {
        firstChunk = false;
        if (!entry.executable && count >= 4 &&
            std::memcmp(workspace.io, "\x7f" "ELF", 4) == 0)
          return PackageVerifyResult::HiddenExecutable;
        if (entry.executable &&
            (count < 20 || std::memcmp(workspace.io, "\x7f" "ELF\x01\x01", 6) != 0 ||
             workspace.io[16] != 3 || workspace.io[17] != 0 ||
             ArchiveDetail::le16(workspace.io + 18) != expectedMachine))
          return PackageVerifyResult::PayloadCorrupt;
      }
      if (!hash.update(workspace.io, count)) return PackageVerifyResult::DigestFailure;
      offset += count;
      remaining -= count;
    }
    if (!hash.finish(digest)) return PackageVerifyResult::DigestFailure;
    uint8_t mismatch = 0;
    for (size_t j = 0; j < sizeof(digest); ++j) {
      mismatch |= static_cast<uint8_t>(entry.sha256[2 * j] != hex[digest[j] >> 4]);
      mismatch |= static_cast<uint8_t>(entry.sha256[2 * j + 1] != hex[digest[j] & 15]);
    }
    if (mismatch) return PackageVerifyResult::PayloadCorrupt;
  }
  return PackageVerifyResult::AuthenticatedContent;
}

} // namespace RuntimePackages
