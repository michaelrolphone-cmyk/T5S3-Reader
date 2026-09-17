#pragma once

#include "PackageArchiveVerification.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RuntimePackages {

// A published generation retains a small, independently verifiable record:
// the original canonical 48-byte header, manifest and 64-byte P-256 signature.
// The record is NOT trusted merely because it lives beside the ELF on SD.
// The firmware verifier rechecks its signature, key scope, runtime policy,
// device security floor, exact directory contents and every entry digest.
// The reserved filename begins with a dot; safePackageEntryName rejects it as
// an archive-controlled entry. This is not an immutable-SD or dlopen pin.
constexpr const char* kPackageProvenanceName = ".risc-auth";
constexpr size_t kPackageProvenanceLimit = kPackageHeaderBytes +
    kPackageManifestLimit + kPackageSignatureBytes;

enum class ProvenanceResult : uint8_t {
  AuthenticatedDirectory, InvalidInput, MalformedRecord, ReadFailure,
  SignatureRejected, PolicyRejected, FloorRejected, UnexpectedFiles,
  MissingEntry, EntryCorrupt
};

namespace ProvenanceDetail {
inline bool digestEquals(const uint8_t actual[32], const char expected[65]) {
  constexpr char hex[] = "0123456789abcdef";
  uint8_t difference = 0;
  for (size_t i = 0; i < 32; ++i) {
    difference |= static_cast<uint8_t>(hex[actual[i] >> 4] != expected[2 * i]);
    difference |= static_cast<uint8_t>(hex[actual[i] & 15] != expected[2 * i + 1]);
  }
  return difference == 0;
}
inline bool bytesEqual(const uint8_t* a, const uint8_t* b, size_t size) {
  uint8_t difference = 0;
  for (size_t i = 0; i < size; ++i) difference |= a[i] ^ b[i];
  return difference == 0;
}
} // namespace ProvenanceDetail

// ReadProvenance: bool(offset, bytes, length), EntrySize: bool(name, uint64_t&),
// ReadEntry: bool(name, offset, bytes, length), ExactEntries: bool(archive).
// ExactEntries MUST enumerate and reject all non-archive files other than the
// single reserved provenance record, including directories and hidden files.
// Hash, signer, resolver and floor obey the archive verifier's existing API.
// Workspace and archive are caller-owned; do not put them on a small task stack.
// expectedSignedPrefixDigest is optional for restart recovery; if provided it
// binds the generation to the manager's previously authenticated intake.
template <typename ReadProvenance, typename EntrySize, typename ReadEntry,
          typename ExactEntries, typename Hash, typename Signer,
          typename Resolver, typename Floor>
ProvenanceResult verifySignedPackageDirectory(ReadProvenance readProvenance,
    uint64_t provenanceLength, EntrySize entrySize, ReadEntry readEntry,
    ExactEntries exactEntries, Hash& hash, Signer& signer, Resolver resolver,
    Floor floor, const PackageRuntimePolicy& policy, PackageArchive& archive,
    PackageVerificationWorkspace& workspace,
    const uint8_t* expectedSignedPrefixDigest = nullptr,
    PackageArchiveLimits limits = {}) {
  archive = {};
  if (!provenanceLength || provenanceLength > kPackageProvenanceLimit ||
      !limits.maxTotalBytes || !policy.architecture || !policy.runtimeApi ||
      limits.maxTotalBytes > std::numeric_limits<uint64_t>::max() -
          kPackageProvenanceLimit)
    return ProvenanceResult::InvalidInput;
  auto reject = [&archive](ProvenanceResult status) {
    archive = {};
    return status;
  };
  uint8_t header[kPackageHeaderBytes]{};
  if (!readProvenance(0, header, sizeof(header)))
    return reject(ProvenanceResult::ReadFailure);
  if (std::memcmp(header, "RISCPKG1", 8))
    return reject(ProvenanceResult::MalformedRecord);
  const uint32_t manifestLength = ArchiveDetail::le32(header + 12);
  if (manifestLength < 16 || manifestLength > kPackageManifestLimit)
    return reject(ProvenanceResult::MalformedRecord);
  const size_t prefixLength = kPackageHeaderBytes + manifestLength;
  if (provenanceLength != prefixLength + kPackageSignatureBytes)
    return reject(ProvenanceResult::MalformedRecord);
  if (!readProvenance(0, workspace.signedPrefix, prefixLength))
    return reject(ProvenanceResult::ReadFailure);
  // Bind decoding and signature hashing to the *same copied prefix*. The SD
  // file might change between read operations; never decode an independent
  // mutable manifest read and subsequently sign a different one.
  if (std::memcmp(header, workspace.signedPrefix, sizeof(header)))
    return reject(ProvenanceResult::MalformedRecord);
  const auto immutablePrefix = [&workspace, prefixLength](uint64_t offset,
      uint8_t* output, size_t length) {
    if (offset > prefixLength || length > prefixLength - offset) return false;
    std::memcpy(output, workspace.signedPrefix + offset, length);
    return true;
  };
  const uint64_t declaredLength = ArchiveDetail::le64(header + 36);
  if (declaredLength > limits.maxTotalBytes + kPackageProvenanceLimit ||
      decodePackageArchive(immutablePrefix, declaredLength, archive,
          workspace.manifestScratch, sizeof(workspace.manifestScratch), limits) !=
              ArchiveResult::ReadyForAuthentication ||
      archive.signatureOffset != prefixLength)
    return reject(ProvenanceResult::MalformedRecord);
  uint8_t digest[32]{};
  if (!hash.start() || !hash.update(workspace.signedPrefix, prefixLength) ||
      !hash.finish(digest)) return reject(ProvenanceResult::SignatureRejected);
  if (expectedSignedPrefixDigest &&
      !ProvenanceDetail::bytesEqual(digest, expectedSignedPrefixDigest, 32))
    return reject(ProvenanceResult::SignatureRejected);
  uint8_t signature[kPackageSignatureBytes]{};
  if (!readProvenance(prefixLength, signature, sizeof(signature)))
    return reject(ProvenanceResult::ReadFailure);
  if (!signer(archive.keyId, digest, signature))
    return reject(ProvenanceResult::SignatureRejected);
  if (preflightArchive(archive, policy, resolver) !=
      PreflightResult::ReadyForContentVerification)
    return reject(ProvenanceResult::PolicyRejected);
  if (!floor(archive)) return reject(ProvenanceResult::FloorRejected);
  if (!exactEntries(archive)) return reject(ProvenanceResult::UnexpectedFiles);
  const uint16_t machine = std::strcmp(archive.architecture, "xtensa-esp32s3") == 0 ?
      94u : std::strcmp(archive.architecture, "riscv32") == 0 ? 243u : 0u;
  if (!machine) return reject(ProvenanceResult::PolicyRejected);
  for (size_t i = 0; i < archive.entryCount; ++i) {
    const ArchiveEntry& entry = archive.entries[i];
    uint64_t observed = 0;
    if (!entrySize(entry.name, observed) || observed != entry.sizeBytes)
      return reject(ProvenanceResult::MissingEntry);
    if (!hash.start()) return reject(ProvenanceResult::EntryCorrupt);
    uint64_t offset = 0;
    while (offset < entry.sizeBytes) {
      const size_t count = entry.sizeBytes - offset < sizeof(workspace.io) ?
          static_cast<size_t>(entry.sizeBytes - offset) : sizeof(workspace.io);
      if (!readEntry(entry.name, offset, workspace.io, count))
        return reject(ProvenanceResult::EntryCorrupt);
      if (offset == 0) {
        if (!entry.executable && count >= 4 &&
            std::memcmp(workspace.io, "\x7f" "ELF", 4) == 0)
          return reject(ProvenanceResult::EntryCorrupt);
        if (entry.executable && (count < 20 ||
            std::memcmp(workspace.io, "\x7f" "ELF\x01\x01", 6) ||
            workspace.io[16] != 3 || workspace.io[17] != 0 ||
            ArchiveDetail::le16(workspace.io + 18) != machine))
          return reject(ProvenanceResult::EntryCorrupt);
      }
      if (!hash.update(workspace.io, count))
        return reject(ProvenanceResult::EntryCorrupt);
      offset += count;
    }
    if (!hash.finish(digest) ||
        !ProvenanceDetail::digestEquals(digest, entry.sha256))
      return reject(ProvenanceResult::EntryCorrupt);
  }
  // Recheck the mutable provenance itself, file inventory and floor after all
  // payload reads. This does not stop physical SD substitution at later load.
  uint8_t verifyPrefix[kPackageHeaderBytes]{};
  if (!readProvenance(0, verifyPrefix, sizeof(verifyPrefix)) ||
      std::memcmp(verifyPrefix, workspace.signedPrefix, sizeof(verifyPrefix)) ||
      !exactEntries(archive) || !floor(archive))
    return reject(ProvenanceResult::UnexpectedFiles);
  // Check the *whole* canonical prefix and signature, not just the header.
  size_t at = 0;
  while (at < prefixLength) {
    const size_t count = prefixLength - at < sizeof(workspace.io) ?
        prefixLength - at : sizeof(workspace.io);
    if (!readProvenance(at, workspace.io, count) ||
        std::memcmp(workspace.io, workspace.signedPrefix + at, count))
      return reject(ProvenanceResult::MalformedRecord);
    at += count;
  }
  uint8_t finalSignature[kPackageSignatureBytes]{};
  if (!readProvenance(prefixLength, finalSignature, sizeof(finalSignature)) ||
      !ProvenanceDetail::bytesEqual(signature, finalSignature, sizeof(signature)))
    return reject(ProvenanceResult::MalformedRecord);
  return ProvenanceResult::AuthenticatedDirectory;
}

} // namespace RuntimePackages
