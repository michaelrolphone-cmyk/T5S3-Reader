#pragma once

#include "PackagePreflight.h"
#include "PackageSecurityFloor.h"
#include "PackageUseGate.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace RuntimePackages {

// Single fixed incoming stage shared across transports; each target/backup is
// derived from the authenticated kind and package ID, never a manifest path.
constexpr const char* kSignedExtractStage = "/Packages/.extract.part";

struct SignedTransactionPaths {
  char target[96]{};
  char backup[96]{};
};

inline bool signedTransactionPaths(Kind kind, const char* id,
                                   SignedTransactionPaths& out) {
  out = {};
  if (!safeId(id)) return false;
  const char* root = nullptr;
  switch (kind) {
    case Kind::Application: root = "/Apps"; break;
    case Kind::Driver: root = "/Drivers"; break;
    case Kind::Service: root = "/Services"; break;
    case Kind::Provider: root = "/Providers"; break;
    default: return false;
  }
  const int targetLength = std::snprintf(out.target, sizeof(out.target),
                                         "%s/%s", root, id);
  const int backupLength = std::snprintf(out.backup, sizeof(out.backup),
                                         "%s/.%s.previous", root, id);
  return targetLength > 0 && backupLength > 0 &&
         static_cast<size_t>(targetLength) < sizeof(out.target) &&
         static_cast<size_t>(backupLength) < sizeof(out.backup);
}

enum class SignedTransactionResult : uint8_t {
  Published, NoInstalledGeneration, RecoveredGeneration, VerifiedGeneration,
  InvalidInput, InUse, StageRejected, TargetRejected, BackupRejected,
  VersionRejected, FloorRejected, RenameFailed, RestoreFailed,
  PostPublishRejected, BackupCleanupPending, FloorCommitPending
};

// Verify: bool(const char* path, const uint8_t* expectedDigest,
//              PackageArchive& out). It MUST verify a firmware-trusted signer,
// exact directory inventory and every payload. expectedDigest is 32 bytes
// when nonnull, binding a stage to the manager's authenticated intake.
// Purge: bool(const char* backup). Purge MUST refuse unknown files and remove
// the provenance file LAST so interrupted cleanup is resumable. It may never
// recursively remove arbitrary SD content. Ops: exists(path), rename(a,b).
// Store: the device-owned monotonic floor interface (never an SD sidecar).
// No capability grant, driver activation or dlopen occurs in these functions.
namespace SignedTransactionDetail {
template <typename Verify, typename Store>
bool accepted(Verify& verify, Store& store, const char* path, Kind kind,
              const char* id, const uint8_t* digest, PackageArchive& observed,
              bool allowFirstInstall) {
  observed = {};
  return verify(path, digest, observed) && observed.identity.kind == kind &&
         std::strcmp(observed.identity.id, id) == 0 &&
         checkPackageSecurityFloor(store, observed, allowFirstInstall) ==
             FloorCheck::Allowed;
}

template <typename Store>
bool advanceAndCheck(Store& store, const PackageArchive& installed,
                     bool allowFirstInstall) {
  const FloorAdvance status = advancePackageSecurityFloor(store, installed);
  return (status == FloorAdvance::Advanced ||
          status == FloorAdvance::AlreadyAtOrAbove) &&
         checkPackageSecurityFloor(store, installed, allowFirstInstall) ==
             FloorCheck::Allowed;
}

// REQUIRES exclusive replacement lease for paths.target. No helper here may
// drop the reservation between rename, backup cleanup and floor advancement.
template <typename Ops, typename Verify, typename Purge, typename Store>
SignedTransactionResult recoverLocked(Ops& ops, const SignedTransactionPaths& paths,
    Kind kind, const char* id, Verify& verify, Purge& purge, Store& store,
    PackageArchive& observed, bool allowFirstInstall) {
  if (ops.exists(paths.backup)) {
    if (!ops.exists(paths.target)) {
      if (!accepted(verify, store, paths.backup, kind, id, nullptr, observed,
                    allowFirstInstall)) return SignedTransactionResult::BackupRejected;
      if (!ops.rename(paths.backup, paths.target))
        return SignedTransactionResult::RenameFailed;
      if (!accepted(verify, store, paths.target, kind, id, nullptr, observed,
                    allowFirstInstall)) {
        (void)ops.rename(paths.target, paths.backup);
        return SignedTransactionResult::RestoreFailed;
      }
      return SignedTransactionResult::RecoveredGeneration;
    }
    // The new target has reached its final name. A partially purged backup is
    // not executable and cleanup is resumed by a selective, provenance-last
    // purge. Never restore a possibly lower-security backup over this target.
    if (!accepted(verify, store, paths.target, kind, id, nullptr, observed,
                  allowFirstInstall)) return SignedTransactionResult::TargetRejected;
    if (!purge(paths.backup)) return SignedTransactionResult::BackupCleanupPending;
    if (!advanceAndCheck(store, observed, allowFirstInstall))
      return SignedTransactionResult::FloorCommitPending;
    return SignedTransactionResult::VerifiedGeneration;
  }
  if (!ops.exists(paths.target)) return SignedTransactionResult::NoInstalledGeneration;
  if (!accepted(verify, store, paths.target, kind, id, nullptr, observed,
                allowFirstInstall)) return SignedTransactionResult::TargetRejected;
  // After a first install, or after backup cleanup, a reset can occur before
  // NVS commit. Recover a fully authenticated target and retry the floor.
  if (!advanceAndCheck(store, observed, allowFirstInstall))
    return SignedTransactionResult::FloorCommitPending;
  return SignedTransactionResult::VerifiedGeneration;
}
} // namespace SignedTransactionDetail

// Use for boot/inventory recovery BEFORE making a signed directory loadable.
// The manager supplies an explicit first-install decision; missing NVS records
// are not silently treated as authorization to accept arbitrary SD files.
template <typename Ops, typename Verify, typename Purge, typename Store>
SignedTransactionResult recoverSignedDirectoryTransaction(Ops& ops, Kind kind,
    const char* id, Verify verify, Purge purge, Store& store,
    PackageArchive& workspace, bool allowFirstInstall = false) {
  SignedTransactionPaths paths{};
  if (!signedTransactionPaths(kind, id, paths))
    return SignedTransactionResult::InvalidInput;
  PackageReplacementLease lease(paths.target);
  if (!lease) return SignedTransactionResult::InUse;
  return SignedTransactionDetail::recoverLocked(ops, paths, kind, id,
      verify, purge, store, workspace, allowFirstInstall);
}

// Publish only a stage already extracted from a trusted signed archive.
// The fingerprint MUST come from successful source intake in privileged RAM.
// It is required again by Verify after the final rename. A newer security
// floor is committed only when a verified target is recoverable and the old
// backup is completely purged, so recovery never needs to run a version that
// the updated floor itself rejects. On NVS failure leave the new target and
// report pending recovery; do not claim a completed install/activate it.
template <typename Ops, typename Verify, typename Purge, typename Store>
SignedTransactionResult publishSignedDirectoryTransaction(Ops& ops,
    const PackageArchive& approved, const uint8_t expectedDigest[32],
    Verify verify, Purge purge, Store& store, PackageArchive& observed,
    bool allowFirstInstall = false, bool allowSemverDowngrade = false) {
  SignedTransactionPaths paths{};
  if (!expectedDigest || !validFloorIdentity(approved) ||
      !signedTransactionPaths(approved.identity.kind, approved.identity.id, paths))
    return SignedTransactionResult::InvalidInput;
  PackageReplacementLease lease(paths.target);
  if (!lease) return SignedTransactionResult::InUse;
  if (!ops.exists(kSignedExtractStage) ||
      !SignedTransactionDetail::accepted(verify, store, kSignedExtractStage,
          approved.identity.kind, approved.identity.id, expectedDigest,
          observed, allowFirstInstall) ||
      std::strcmp(approved.identity.version, observed.identity.version) != 0 ||
      std::strcmp(approved.identity.artifact, observed.identity.artifact) != 0 ||
      approved.securityVersion != observed.securityVersion ||
      approved.keyId != observed.keyId)
    return SignedTransactionResult::StageRejected;

  SignedTransactionResult recovered = SignedTransactionDetail::recoverLocked(
      ops, paths, approved.identity.kind, approved.identity.id,
      verify, purge, store, observed, allowFirstInstall);
  if (recovered != SignedTransactionResult::NoInstalledGeneration &&
      recovered != SignedTransactionResult::RecoveredGeneration &&
      recovered != SignedTransactionResult::VerifiedGeneration)
    return recovered;
  const bool hadTarget = ops.exists(paths.target);
  if (hadTarget) {
    if (!SignedTransactionDetail::accepted(verify, store, paths.target,
            approved.identity.kind, approved.identity.id, nullptr, observed,
            allowFirstInstall)) return SignedTransactionResult::TargetRejected;
    const InstallDecision decision = decidePackageVersion(approved.identity,
        &observed.identity, allowSemverDowngrade);
    if (decision != InstallDecision::Upgrade &&
        decision != InstallDecision::DowngradeAllowed)
      return SignedTransactionResult::VersionRejected;
  }
  // Recovering a previous update may have raised the NVS floor after the
  // initial stage check; validate it a second time under the target lease.
  if (checkPackageSecurityFloor(store, approved, allowFirstInstall) !=
      FloorCheck::Allowed) return SignedTransactionResult::FloorRejected;
  if (!ops.exists(kSignedExtractStage) ||
      !SignedTransactionDetail::accepted(verify, store, kSignedExtractStage,
          approved.identity.kind, approved.identity.id, expectedDigest,
          observed, allowFirstInstall)) return SignedTransactionResult::StageRejected;
  if (hadTarget && !ops.rename(paths.target, paths.backup))
    return SignedTransactionResult::RenameFailed;
  if (!ops.rename(kSignedExtractStage, paths.target)) {
    if (hadTarget && !ops.rename(paths.backup, paths.target))
      return SignedTransactionResult::RestoreFailed;
    return SignedTransactionResult::RenameFailed;
  }
  if (!SignedTransactionDetail::accepted(verify, store, paths.target,
          approved.identity.kind, approved.identity.id, expectedDigest,
          observed, allowFirstInstall)) {
    // Restore only if we can move the suspect target to its disposable stage.
    // Never delete or overwrite an unverified target after a failed rename.
    if (!ops.rename(paths.target, kSignedExtractStage))
      return SignedTransactionResult::RestoreFailed;
    if (hadTarget && !ops.rename(paths.backup, paths.target))
      return SignedTransactionResult::RestoreFailed;
    return SignedTransactionResult::PostPublishRejected;
  }
  if (hadTarget && !purge(paths.backup))
    return SignedTransactionResult::BackupCleanupPending;
  if (!SignedTransactionDetail::accepted(verify, store, paths.target,
          approved.identity.kind, approved.identity.id, expectedDigest,
          observed, allowFirstInstall)) return SignedTransactionResult::PostPublishRejected;
  if (!SignedTransactionDetail::advanceAndCheck(store, observed, allowFirstInstall))
    return SignedTransactionResult::FloorCommitPending;
  return SignedTransactionResult::Published;
}

} // namespace RuntimePackages
