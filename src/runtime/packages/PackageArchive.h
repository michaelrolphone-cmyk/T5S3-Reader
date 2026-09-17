#pragma once

#include "PackagePreflight.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace RuntimePackages {

// RISC-PKG v1 is an uncompressed, canonical, contiguous-entry envelope.
// This decoder checks framing and metadata only. It does NOT authenticate a
// publisher, hash payloads, install packages, or authorize any capability.
// Fail closed: callers MUST authenticate the signed prefix and every entry
// before publication, and bind the verified bytes to the eventual ELF load.
constexpr size_t kPackageHeaderBytes = 48;
constexpr size_t kPackageManifestLimit = 4096;
constexpr size_t kPackageSignatureBytes = 64;
constexpr uint16_t kPackageSignatureP256Sha256 = 1;

struct ArchiveEntry {
  char name[128]{};
  uint64_t sizeBytes = 0;
  uint64_t offset = 0;
  char sha256[65]{};
  bool executable = false;
};

struct ArchiveRequirement {
  char capability[64]{};
  uint32_t minApi = 0;
};

struct PackageArchive {
  Identity identity{};
  char architecture[32]{};
  uint32_t minRuntimeApi = 0;
  uint32_t securityVersion = 0;
  uint32_t keyId = 0;
  uint16_t signatureAlgorithm = 0;
  uint16_t entryCount = 0;
  uint16_t requirementCount = 0;
  uint64_t signatureOffset = 0;
  uint64_t payloadOffset = 0;
  uint64_t fileLength = 0;
  ArchiveEntry entries[kMaxPackageEntries]{};
  ArchiveRequirement requirements[kMaxPackageRequirements]{};
};

struct PackageArchiveLimits {
  uint64_t maxEntryBytes = 1024u * 1024u;
  uint64_t maxTotalBytes = 4u * 1024u * 1024u;
};

enum class ArchiveResult : uint8_t {
  ReadyForAuthentication, InvalidInput, InvalidHeader, InvalidLength,
  InvalidMetadata, InvalidEntry, InvalidRequirement, ReadFailure
};

namespace ArchiveDetail {
inline uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1]) << 8;
}
inline uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(le16(p)) | static_cast<uint32_t>(le16(p + 2)) << 16;
}
inline uint64_t le64(const uint8_t* p) {
  return static_cast<uint64_t>(le32(p)) | static_cast<uint64_t>(le32(p + 4)) << 32;
}
inline bool canonicalVersion(const char* text) {
  if (!safeVersion(text)) return false;
  const char* part = text;
  for (const char* p = text;; ++p) {
    if (*p != '\0' && *p != '.') continue;
    if (p - part > 1 && part[0] == '0') return false;
    if (!*p) return true;
    part = p + 1;
  }
}
inline bool add(uint64_t a, uint64_t b, uint64_t& result) {
  if (b > std::numeric_limits<uint64_t>::max() - a) return false;
  result = a + b;
  return true;
}
inline bool name(const uint8_t* data, size_t length, char* dest, size_t capacity) {
  if (!length || length >= capacity) return false;
  for (size_t i = 0; i < length; ++i) {
    if (data[i] < 0x21 || data[i] > 0x7e) return false;
    dest[i] = static_cast<char>(data[i]);
  }
  dest[length] = '\0';
  return true;
}
inline void hexDigest(const uint8_t* digest, char out[65]) {
  constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < 32; ++i) {
    out[2 * i] = digits[digest[i] >> 4];
    out[2 * i + 1] = digits[digest[i] & 15];
  }
  out[64] = '\0';
}
} // namespace ArchiveDetail

// readAt(offset, destination, length) must return true only for an exact read.
// scratch is caller-owned, at least manifest length bytes; no ELF-size buffer,
// allocations, filesystem mutations, or candidate execution are needed.
template <typename ReadAt>
ArchiveResult decodePackageArchive(ReadAt readAt, uint64_t fileLength,
                                   PackageArchive& out, uint8_t* scratch,
                                   size_t scratchCapacity,
                                   PackageArchiveLimits limits = {}) {
  out = {};
  if (!scratch || !limits.maxEntryBytes || !limits.maxTotalBytes ||
      fileLength < kPackageHeaderBytes) return ArchiveResult::InvalidInput;
  uint8_t header[kPackageHeaderBytes]{};
  if (!readAt(0, header, sizeof(header))) return ArchiveResult::ReadFailure;
  if (std::memcmp(header, "RISCPKG1", 8) || ArchiveDetail::le16(header + 8) != 1 ||
      ArchiveDetail::le16(header + 10) != kPackageSignatureP256Sha256 ||
      ArchiveDetail::le16(header + 20) != kPackageSignatureBytes ||
      ArchiveDetail::le16(header + 22) || ArchiveDetail::le32(header + 44))
    return ArchiveResult::InvalidHeader;
  const uint32_t manifestLength = ArchiveDetail::le32(header + 12);
  const uint16_t entryCount = ArchiveDetail::le16(header + 16);
  const uint16_t requirementCount = ArchiveDetail::le16(header + 18);
  const uint64_t payloadLength = ArchiveDetail::le64(header + 24);
  const uint32_t keyId = ArchiveDetail::le32(header + 32);
  const uint64_t declaredLength = ArchiveDetail::le64(header + 36);
  uint64_t signatureOffset = 0, payloadOffset = 0, computedLength = 0;
  if (manifestLength < 16 || manifestLength > kPackageManifestLimit ||
      manifestLength > scratchCapacity || !entryCount || entryCount > kMaxPackageEntries ||
      requirementCount > kMaxPackageRequirements || !keyId ||
      !payloadLength || payloadLength > limits.maxTotalBytes ||
      !ArchiveDetail::add(kPackageHeaderBytes, manifestLength, signatureOffset) ||
      !ArchiveDetail::add(signatureOffset, kPackageSignatureBytes, payloadOffset) ||
      !ArchiveDetail::add(payloadOffset, payloadLength, computedLength) ||
      computedLength != fileLength || declaredLength != fileLength)
    return ArchiveResult::InvalidLength;
  if (!readAt(kPackageHeaderBytes, scratch, manifestLength)) return ArchiveResult::ReadFailure;
  const uint8_t* p = scratch;
  const uint8_t* const end = scratch + manifestLength;
  const uint8_t kind = p[0];
  const size_t idLength = p[1], versionLength = p[2], artifactLength = p[3], architectureLength = p[4];
  if (kind > static_cast<uint8_t>(Kind::Provider) || p[5] || ArchiveDetail::le16(p + 6) ||
      !idLength || !versionLength || !artifactLength || !architectureLength ||
      idLength >= sizeof(Identity::id) || versionLength >= sizeof(Identity::version) ||
      artifactLength >= sizeof(Identity::artifact) || architectureLength >= sizeof(out.architecture))
    return ArchiveResult::InvalidMetadata;
  out.minRuntimeApi = ArchiveDetail::le32(p + 8);
  out.securityVersion = ArchiveDetail::le32(p + 12);
  p += 16;
  const uint64_t nameLength = idLength + versionLength + artifactLength + architectureLength;
  if (static_cast<uint64_t>(end - p) < nameLength) return ArchiveResult::InvalidMetadata;
  char id[sizeof(Identity::id)]{}, version[sizeof(Identity::version)]{}, artifact[sizeof(Identity::artifact)]{};
  if (!ArchiveDetail::name(p, idLength, id, sizeof(id))) return ArchiveResult::InvalidMetadata;
  p += idLength;
  if (!ArchiveDetail::name(p, versionLength, version, sizeof(version))) return ArchiveResult::InvalidMetadata;
  p += versionLength;
  if (!ArchiveDetail::name(p, artifactLength, artifact, sizeof(artifact))) return ArchiveResult::InvalidMetadata;
  p += artifactLength;
  if (!ArchiveDetail::name(p, architectureLength, out.architecture, sizeof(out.architecture)))
    return ArchiveResult::InvalidMetadata;
  p += architectureLength;
  if (!makeIdentity(static_cast<Kind>(kind), id, version, artifact, false, &out.identity) ||
      !ArchiveDetail::canonicalVersion(version) || !out.minRuntimeApi ||
      !out.securityVersion || !safePackageCapability(out.architecture))
    return ArchiveResult::InvalidMetadata;
  // Binary encoding is canonical: entries and requirements are strictly
  // sorted, fields are fixed-width little endian and all reserved bits zero.
  uint64_t offset = payloadOffset, total = 0;
  for (uint16_t i = 0; i < entryCount; ++i) {
    if (end - p < 42) return ArchiveResult::InvalidEntry;
    const size_t nameLength = p[0];
    const uint8_t flags = p[1];
    const uint64_t bytes = ArchiveDetail::le64(p + 2);
    if (flags > 1 || !bytes || bytes > limits.maxEntryBytes ||
        nameLength == 0 || nameLength >= sizeof(ArchiveEntry::name) ||
        static_cast<uint64_t>(end - p) < 42 + nameLength ||
        bytes > payloadLength - total) return ArchiveResult::InvalidEntry;
    auto& entry = out.entries[i];
    if (!ArchiveDetail::name(p + 42, nameLength, entry.name, sizeof(entry.name)) ||
        !safePackageEntryName(entry.name) ||
        (i && std::strcmp(out.entries[i - 1].name, entry.name) >= 0))
      return ArchiveResult::InvalidEntry;
    entry.executable = flags == 1;
    entry.sizeBytes = bytes;
    entry.offset = offset;
    ArchiveDetail::hexDigest(p + 10, entry.sha256);
    if (!ArchiveDetail::add(offset, bytes, offset)) return ArchiveResult::InvalidEntry;
    total += bytes;
    p += 42 + nameLength;
  }
  if (total != payloadLength) return ArchiveResult::InvalidEntry;
  for (uint16_t i = 0; i < requirementCount; ++i) {
    if (end - p < 5) return ArchiveResult::InvalidRequirement;
    const size_t nameLength = p[0];
    if (!nameLength || nameLength >= sizeof(ArchiveRequirement::capability) ||
        static_cast<uint64_t>(end - p) < 5 + nameLength)
      return ArchiveResult::InvalidRequirement;
    auto& req = out.requirements[i];
    req.minApi = ArchiveDetail::le32(p + 1);
    if (!ArchiveDetail::name(p + 5, nameLength, req.capability, sizeof(req.capability)) ||
        !safePackageCapability(req.capability) || !req.minApi ||
        (i && std::strcmp(out.requirements[i - 1].capability, req.capability) >= 0))
      return ArchiveResult::InvalidRequirement;
    p += 5 + nameLength;
  }
  if (p != end) return ArchiveResult::InvalidMetadata;
  out.keyId = keyId;
  out.signatureAlgorithm = kPackageSignatureP256Sha256;
  out.entryCount = entryCount;
  out.requirementCount = requirementCount;
  out.signatureOffset = signatureOffset;
  out.payloadOffset = payloadOffset;
  out.fileLength = fileLength;
  return ArchiveResult::ReadyForAuthentication;
}

// Reuse the existing four-kind preflight policy; no filesystem or execution.
template <typename Resolver>
PreflightResult preflightArchive(const PackageArchive& archive,
                                 const PackageRuntimePolicy& policy, Resolver resolver) {
  PackageEntry entries[kMaxPackageEntries]{};
  PackageRequirement requirements[kMaxPackageRequirements]{};
  for (size_t i = 0; i < archive.entryCount && i < kMaxPackageEntries; ++i) {
    const auto& e = archive.entries[i];
    entries[i] = {e.name, e.sizeBytes, e.sha256, e.executable};
  }
  for (size_t i = 0; i < archive.requirementCount && i < kMaxPackageRequirements; ++i) {
    const auto& r = archive.requirements[i];
    requirements[i] = {r.capability, r.minApi};
  }
  const PackageEnvelopeView view{archive.identity, archive.architecture,
      archive.minRuntimeApi, archive.securityVersion, entries, archive.entryCount,
      requirements, archive.requirementCount};
  return preflightPackage(view, policy, resolver);
}

} // namespace RuntimePackages
