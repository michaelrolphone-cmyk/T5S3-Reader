#pragma once

#include "PackageArchiveVerification.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimePackages {

// A stage owns ONLY its own fresh, disposable file. begin(length), append(data,
// length), seal(), readAt(offset,data,length) and discard() are provided by a
// privileged storage adapter; neither paths nor open handles are constructed
// from an archive's untrusted metadata. No published installation is modified.
//
// Authentication is deliberately repeated AFTER copying from mutable input:
// changes during an SD/network copy are caught, not installed. The stage must
// still be protected from subsequent modification through publication/load.
// Successful staging is NOT install authorization or verified-byte pinning.
enum class ArchiveStageResult : uint8_t {
  ReadyForPublicationReview, InvalidInput, SourceUntrusted, PreflightRejected,
  StageUnavailable, CopyFailure, SealFailure, StageUntrusted, DifferentPackage
};

template <typename SourceRead, typename Stage, typename Hash,
          typename SignatureVerifier, typename Resolver>
ArchiveStageResult stageSignedPackageArchive(SourceRead source, uint64_t length,
    Stage& stage, Hash& hash, SignatureVerifier& signer, Resolver resolver,
    const PackageRuntimePolicy& policy, PackageArchive& archive,
    PackageVerificationWorkspace& workspace, PackageArchiveLimits limits = {}) {
  archive = {};
  if (!length || !limits.maxTotalBytes || length > limits.maxTotalBytes +
      kPackageHeaderBytes + kPackageManifestLimit + kPackageSignatureBytes)
    return ArchiveStageResult::InvalidInput;
  if (verifyPackageArchive(source, length, archive, workspace, hash, signer, limits) !=
      PackageVerifyResult::AuthenticatedContent) {
    archive = {};
    return ArchiveStageResult::SourceUntrusted;
  }
  if (preflightArchive(archive, policy, resolver) !=
      PreflightResult::ReadyForContentVerification) {
    archive = {};
    return ArchiveStageResult::PreflightRejected;
  }
  const size_t prefixLength = static_cast<size_t>(archive.signatureOffset);
  uint8_t originalPrefixDigest[32]{};
  if (!hash.start() || !hash.update(workspace.signedPrefix, prefixLength) ||
      !hash.finish(originalPrefixDigest)) {
    archive = {};
    return ArchiveStageResult::SourceUntrusted;
  }
  if (!stage.begin(length)) {
    archive = {};
    return ArchiveStageResult::StageUnavailable;
  }
  uint64_t offset = 0;
  while (offset < length) {
    const size_t count = length - offset < sizeof(workspace.io) ?
        static_cast<size_t>(length - offset) : sizeof(workspace.io);
    if (!source(offset, workspace.io, count) || !stage.append(workspace.io, count)) {
      (void)stage.discard();
      archive = {};
      return ArchiveStageResult::CopyFailure;
    }
    offset += count;
  }
  if (!stage.seal()) {
    (void)stage.discard();
    archive = {};
    return ArchiveStageResult::SealFailure;
  }
  // The verifier reads the sealed copy rather than trusting the source or the
  // return status of a filesystem write. A valid-but-different signed archive
  // does not meet the identity of the inspected candidate either.
  auto stagedRead = [&stage](uint64_t at, uint8_t* bytes, size_t count) {
    return stage.readAt(at, bytes, count);
  };
  if (verifyPackageArchive(stagedRead, length, archive, workspace, hash, signer, limits) !=
      PackageVerifyResult::AuthenticatedContent ||
      preflightArchive(archive, policy, resolver) !=
          PreflightResult::ReadyForContentVerification) {
    (void)stage.discard();
    archive = {};
    return ArchiveStageResult::StageUntrusted;
  }
  uint8_t stagedPrefixDigest[32]{};
  if (static_cast<size_t>(archive.signatureOffset) != prefixLength ||
      !hash.start() || !hash.update(workspace.signedPrefix, prefixLength) ||
      !hash.finish(stagedPrefixDigest)) {
    (void)stage.discard();
    archive = {};
    return ArchiveStageResult::StageUntrusted;
  }
  uint8_t mismatch = 0;
  for (size_t i = 0; i < 32; ++i)
    mismatch |= static_cast<uint8_t>(originalPrefixDigest[i] ^ stagedPrefixDigest[i]);
  if (mismatch) {
    (void)stage.discard();
    archive = {};
    return ArchiveStageResult::DifferentPackage;
  }
  return ArchiveStageResult::ReadyForPublicationReview;
}

} // namespace RuntimePackages
