#pragma once

#include "PackageArchiveVerification.h"
#include "PackageSecurityFloor.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimePackages {

// This is extraction into a disposable, NOT published, directory. A privileged
// destination must implement begin(archive), beginEntry(name,length),
// append(data,length), endEntry(), readEntry(name,offset,data,length), seal(),
// and discard(). It must own its own fixed/validated stage path, exclusively
// create files, close a writer before readback, and never touch installed data.
// begin() failure must be safe to follow by discard(). No path may be supplied
// by an archive without validating its identity and entry basenames.
//
// The expected digest MUST be captured from the authenticated intake's signed
// prefix by the manager and held outside mutable SD. Do not compute it for the
// first time from a potentially replaced intake during extraction. This does
// not make the final extracted bytes immutable until dlopen: publication,
// crash recovery, provenance retention and the load lifetime remain separate.
enum class ArchiveExtractResult : uint8_t {
  ReadyForPublicationReview, InvalidInput, IntakeUntrusted, DifferentPackage,
  PreflightRejected, FloorRejected, StageUnavailable, CopyFailure,
  EntryCorrupt, ReadbackFailure, SealFailure, IntakeChanged
};

namespace ExtractDetail {
inline bool sameDigest(const uint8_t* a, const uint8_t* b) {
  uint8_t difference = 0;
  for (size_t i = 0; i < 32; ++i)
    difference |= static_cast<uint8_t>(a[i] ^ b[i]);
  return difference == 0;
}

inline bool matchesHex(const uint8_t digest[32], const char expected[65]) {
  constexpr char hex[] = "0123456789abcdef";
  uint8_t difference = 0;
  for (size_t i = 0; i < 32; ++i) {
    difference |= static_cast<uint8_t>(hex[digest[i] >> 4] != expected[i * 2]);
    difference |= static_cast<uint8_t>(hex[digest[i] & 15] != expected[i * 2 + 1]);
  }
  return difference == 0;
}
} // namespace ExtractDetail

// ReadAt: bool(uint64_t,uint8_t*,size_t), Hash: start/update/finish,
// Signer: bool(keyId,digest,signature), Resolver: uint32_t(capability),
// Floor: bool(const PackageArchive&), supplied ONLY by privileged policy.
// Archive and workspace are caller-owned (workspace is >8 KiB, not task stack).
template <typename ReadAt, typename Destination, typename Hash,
          typename Signer, typename Resolver, typename Floor>
ArchiveExtractResult extractSignedPackageArchive(ReadAt source, uint64_t length,
    const uint8_t expectedSignedPrefixDigest[32], Destination& destination,
    Hash& hash, Signer& signer, Resolver resolver, Floor floor,
    const PackageRuntimePolicy& policy, PackageArchive& archive,
    PackageVerificationWorkspace& workspace, PackageArchiveLimits limits = {}) {
  archive = {};
  if (!expectedSignedPrefixDigest || length < kPackageHeaderBytes ||
      !limits.maxTotalBytes || !policy.architecture || !policy.runtimeApi)
    return ArchiveExtractResult::InvalidInput;
  if (verifyPackageArchive(source, length, archive, workspace, hash, signer,
                           limits) != PackageVerifyResult::AuthenticatedContent) {
    archive = {};
    return ArchiveExtractResult::IntakeUntrusted;
  }
  uint8_t digest[32]{};
  const size_t prefixLength = static_cast<size_t>(archive.signatureOffset);
  if (!hash.start() || !hash.update(workspace.signedPrefix, prefixLength) ||
      !hash.finish(digest) ||
      !ExtractDetail::sameDigest(digest, expectedSignedPrefixDigest)) {
    archive = {};
    return ArchiveExtractResult::DifferentPackage;
  }
  if (preflightArchive(archive, policy, resolver) !=
      PreflightResult::ReadyForContentVerification) {
    archive = {};
    return ArchiveExtractResult::PreflightRejected;
  }
  if (!floor(archive)) {
    archive = {};
    return ArchiveExtractResult::FloorRejected;
  }
  // Capture the signed expected entry digests in the decoded archive. During
  // extraction do NOT replace this archive with a fresh decode until every
  // output file has been checked against its original signed entry digest.
  if (!destination.begin(archive)) {
    (void)destination.discard();
    archive = {};
    return ArchiveExtractResult::StageUnavailable;
  }
  auto fail = [&destination, &archive](ArchiveExtractResult why) {
    (void)destination.discard();
    archive = {};
    return why;
  };
  for (size_t i = 0; i < archive.entryCount; ++i) {
    const ArchiveEntry& entry = archive.entries[i];
    if (!destination.beginEntry(entry.name, entry.sizeBytes) || !hash.start())
      return fail(ArchiveExtractResult::CopyFailure);
    uint64_t at = entry.offset;
    uint64_t remaining = entry.sizeBytes;
    while (remaining) {
      const size_t count = remaining < sizeof(workspace.io) ?
          static_cast<size_t>(remaining) : sizeof(workspace.io);
      if (!source(at, workspace.io, count) ||
          !hash.update(workspace.io, count) ||
          !destination.append(workspace.io, count))
        return fail(ArchiveExtractResult::CopyFailure);
      remaining -= count;
      at += count;
    }
    if (!hash.finish(digest) || !ExtractDetail::matchesHex(digest, entry.sha256))
      return fail(ArchiveExtractResult::EntryCorrupt);
    if (!destination.endEntry() || !hash.start())
      return fail(ArchiveExtractResult::ReadbackFailure);
    at = 0;
    remaining = entry.sizeBytes;
    while (remaining) {
      const size_t count = remaining < sizeof(workspace.io) ?
          static_cast<size_t>(remaining) : sizeof(workspace.io);
      if (!destination.readEntry(entry.name, at, workspace.io, count) ||
          !hash.update(workspace.io, count))
        return fail(ArchiveExtractResult::ReadbackFailure);
      remaining -= count;
      at += count;
    }
    if (!hash.finish(digest) || !ExtractDetail::matchesHex(digest, entry.sha256))
      return fail(ArchiveExtractResult::ReadbackFailure);
  }
  if (!destination.seal()) return fail(ArchiveExtractResult::SealFailure);

  // Detect modification of the intake during extraction, even if each output
  // happened to hash correctly. Bind against the originally approved prefix.
  if (verifyPackageArchive(source, length, archive, workspace, hash, signer,
                           limits) != PackageVerifyResult::AuthenticatedContent)
    return fail(ArchiveExtractResult::IntakeChanged);
  if (static_cast<size_t>(archive.signatureOffset) != prefixLength ||
      !hash.start() || !hash.update(workspace.signedPrefix, prefixLength) ||
      !hash.finish(digest) ||
      !ExtractDetail::sameDigest(digest, expectedSignedPrefixDigest))
    return fail(ArchiveExtractResult::IntakeChanged);
  if (preflightArchive(archive, policy, resolver) !=
          PreflightResult::ReadyForContentVerification || !floor(archive))
    return fail(ArchiveExtractResult::FloorRejected);
  return ArchiveExtractResult::ReadyForPublicationReview;
}

} // namespace RuntimePackages
