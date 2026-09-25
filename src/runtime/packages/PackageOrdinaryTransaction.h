#pragma once

#include "PackagePreflight.h"
#include "PackageUseGate.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace RuntimePackages {

// The same four-kind namespace and transaction algorithm is used for offline
// SD and downloaded ordinary (UNSIGNED) packages. No signing keys, security
// floors, hardware ownership or activation are involved.
struct OrdinaryTransactionPaths {
  char target[96]{};
  char stage[96]{};
  char backup[96]{};
  char removing[96]{};
};

inline bool ordinaryTransactionPaths(Kind kind, const char* id,
                                     OrdinaryTransactionPaths& out) {
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
  const int target = std::snprintf(out.target, sizeof(out.target), "%s/%s", root, id);
  const int stage = std::snprintf(out.stage, sizeof(out.stage), "%s/.%s.pkg-stage", root, id);
  const int backup = std::snprintf(out.backup, sizeof(out.backup), "%s/.%s.pkg-previous", root, id);
  const int removing = std::snprintf(out.removing, sizeof(out.removing), "%s/.%s.pkg-removing", root, id);
  return target > 0 && stage > 0 && backup > 0 && removing > 0 &&
      static_cast<size_t>(target) < sizeof(out.target) &&
      static_cast<size_t>(stage) < sizeof(out.stage) &&
      static_cast<size_t>(backup) < sizeof(out.backup) &&
      static_cast<size_t>(removing) < sizeof(out.removing);
}

enum class OrdinaryTransactionResult : uint8_t {
  Published, NoInstalledPackage, InstalledVerified, PreviousRestored,
  Removed, InvalidIdentity, InUse, InvalidStage, InvalidInstalled,
  VersionRejected, RenameFailed, RestorePending, CleanupPending,
  RemovalPending, AmbiguousState
};

namespace OrdinaryTransactionDetail {
// Verify(path, Identity&) re-parses its retained bounded manifest, hashes every
// file against self-declared SHA-256 and rejects any extra file/directory.
// Purge(path) ONLY removes manager-owned known entries, verifying the full
// directory inventory BEFORE deleting anything; it must resume partial
// cleanup by deleting known remaining entries, with the manifest LAST.
// Ops implements exists(path), rename(src,dst); all mutations use HalStorage.
inline bool validObserved(const Identity& observed, Kind kind, const char* id) {
  Identity canonical{};
  return makeIdentity(observed.kind, observed.id, observed.version,
                      observed.artifact, false, &canonical) &&
         observed.kind == kind && std::strcmp(observed.id, id) == 0;
}

template <typename Verify>
bool inspect(Verify& verify, const char* path, Kind kind, const char* id,
             Identity& observed) {
  observed = {};
  if (!verify(path, observed) || !validObserved(observed, kind, id)) {
    observed = {};
    return false;
  }
  return true;
}

// Requires the exclusive replacement lease for `paths.target`; also used
// by boot recovery before a managed package may be considered available.
template <typename Ops, typename Verify, typename Purge>
OrdinaryTransactionResult recoverLocked(Ops& ops,
    const OrdinaryTransactionPaths& paths, Kind kind, const char* id,
    Verify& verify, Purge& purge, Identity& observed) {
  if (ops.exists(paths.removing)) {
    // An uninstall is already committed by the target->removing rename. It
    // must NEVER resurrect the old executable after an interrupted deletion.
    if (ops.exists(paths.target) || ops.exists(paths.backup))
      return OrdinaryTransactionResult::AmbiguousState;
    if (!purge(paths.removing)) return OrdinaryTransactionResult::RemovalPending;
    return OrdinaryTransactionResult::Removed;
  }
  if (ops.exists(paths.backup)) {
    if (!ops.exists(paths.target)) {
      if (!inspect(verify, paths.backup, kind, id, observed))
        return OrdinaryTransactionResult::InvalidInstalled;
      if (!ops.rename(paths.backup, paths.target))
        return OrdinaryTransactionResult::RenameFailed;
      if (!inspect(verify, paths.target, kind, id, observed)) {
        (void)ops.rename(paths.target, paths.backup);
        return OrdinaryTransactionResult::RestorePending;
      }
      return OrdinaryTransactionResult::PreviousRestored;
    }
    if (!inspect(verify, paths.target, kind, id, observed))
      return OrdinaryTransactionResult::InvalidInstalled;
    if (!purge(paths.backup)) return OrdinaryTransactionResult::CleanupPending;
    return OrdinaryTransactionResult::InstalledVerified;
  }
  if (!ops.exists(paths.target)) return OrdinaryTransactionResult::NoInstalledPackage;
  return inspect(verify, paths.target, kind, id, observed) ?
      OrdinaryTransactionResult::InstalledVerified :
      OrdinaryTransactionResult::InvalidInstalled;
}
} // namespace OrdinaryTransactionDetail

template <typename Ops, typename Verify, typename Purge>
OrdinaryTransactionResult recoverOrdinaryPackage(Ops& ops, Kind kind,
    const char* id, Verify verify, Purge purge, Identity& observed) {
  OrdinaryTransactionPaths paths{};
  if (!ordinaryTransactionPaths(kind, id, paths))
    return OrdinaryTransactionResult::InvalidIdentity;
  // Normal inventory is read-only when there is no interrupted transaction.
  if (!ops.exists(paths.backup) && !ops.exists(paths.removing)) {
    if (!ops.exists(paths.target)) return OrdinaryTransactionResult::NoInstalledPackage;
    return OrdinaryTransactionDetail::inspect(verify, paths.target, kind, id,
        observed) ? OrdinaryTransactionResult::InstalledVerified :
                    OrdinaryTransactionResult::InvalidInstalled;
  }
  PackageReplacementLease lease(paths.target);
  if (!lease) return OrdinaryTransactionResult::InUse;
  return OrdinaryTransactionDetail::recoverLocked(ops, paths, kind, id,
      verify, purge, observed);
}

// The target and staged directory have already been fully integrity-checked;
// this re-verifies both under the exclusive identity lease before publication.
// Normal callers accept only a NEWER semver or a fresh installation. An
// explicit package-manager replacement may allow an OLDER semver, but neither
// metadata nor the caller can bypass the active mapping gate or identity check.
template <typename Ops, typename Verify, typename Purge>
OrdinaryTransactionResult publishOrdinaryPackage(Ops& ops,
    const Identity& candidate, Verify verify, Purge purge, Identity& observed,
    bool allowDowngrade = false) {
  OrdinaryTransactionPaths paths{};
  Identity canonical{};
  if (!makeIdentity(candidate.kind, candidate.id, candidate.version,
                    candidate.artifact, false, &canonical) ||
      candidate.legacyVersion ||
      !ordinaryTransactionPaths(candidate.kind, candidate.id, paths))
    return OrdinaryTransactionResult::InvalidIdentity;
  PackageReplacementLease lease(paths.target);
  if (!lease) return OrdinaryTransactionResult::InUse;
  if (!ops.exists(paths.stage) ||
      !OrdinaryTransactionDetail::inspect(verify, paths.stage,
          candidate.kind, candidate.id, observed) ||
      std::strcmp(candidate.version, observed.version) != 0 ||
      std::strcmp(candidate.artifact, observed.artifact) != 0)
    return OrdinaryTransactionResult::InvalidStage;
  const auto recovered = OrdinaryTransactionDetail::recoverLocked(ops, paths,
      candidate.kind, candidate.id, verify, purge, observed);
  if (recovered != OrdinaryTransactionResult::InstalledVerified &&
      recovered != OrdinaryTransactionResult::PreviousRestored &&
      recovered != OrdinaryTransactionResult::NoInstalledPackage)
    return recovered;
  const bool installed = ops.exists(paths.target);
  if (installed) {
    if (!OrdinaryTransactionDetail::inspect(verify, paths.target,
        candidate.kind, candidate.id, observed))
      return OrdinaryTransactionResult::InvalidInstalled;
    const auto decision = decidePackageVersion(candidate, &observed, allowDowngrade);
    if (decision != InstallDecision::Upgrade &&
        decision != InstallDecision::DowngradeAllowed)
      return OrdinaryTransactionResult::VersionRejected;
  }
  if (!OrdinaryTransactionDetail::inspect(verify, paths.stage,
      candidate.kind, candidate.id, observed) ||
      std::strcmp(candidate.version, observed.version) != 0)
    return OrdinaryTransactionResult::InvalidStage;
  if (installed && !ops.rename(paths.target, paths.backup))
    return OrdinaryTransactionResult::RenameFailed;
  if (!ops.rename(paths.stage, paths.target)) {
    if (installed && !ops.rename(paths.backup, paths.target))
      return OrdinaryTransactionResult::RestorePending;
    return OrdinaryTransactionResult::RenameFailed;
  }
  if (!OrdinaryTransactionDetail::inspect(verify, paths.target,
          candidate.kind, candidate.id, observed) ||
      std::strcmp(candidate.version, observed.version) != 0) {
    if (!ops.rename(paths.target, paths.stage))
      return OrdinaryTransactionResult::RestorePending;
    if (installed && !ops.rename(paths.backup, paths.target))
      return OrdinaryTransactionResult::RestorePending;
    return OrdinaryTransactionResult::InvalidInstalled;
  }
  if (installed && !purge(paths.backup))
    return OrdinaryTransactionResult::CleanupPending;
  return OrdinaryTransactionResult::Published;
}

// Uninstallation never activates any package or grants a capability. The
// rename to a manager-owned tombstone commits the removal request. Recovery
// retries selective purge after an interrupted removal, never reinstalls it.
template <typename Ops, typename Verify, typename Purge>
OrdinaryTransactionResult uninstallOrdinaryPackage(Ops& ops, Kind kind,
    const char* id, Verify verify, Purge purge, Identity& observed) {
  OrdinaryTransactionPaths paths{};
  if (!ordinaryTransactionPaths(kind, id, paths))
    return OrdinaryTransactionResult::InvalidIdentity;
  PackageReplacementLease lease(paths.target);
  if (!lease) return OrdinaryTransactionResult::InUse;
  const auto recovery = OrdinaryTransactionDetail::recoverLocked(ops, paths,
      kind, id, verify, purge, observed);
  if (recovery == OrdinaryTransactionResult::NoInstalledPackage ||
      recovery == OrdinaryTransactionResult::Removed) return recovery;
  if (recovery != OrdinaryTransactionResult::InstalledVerified &&
      recovery != OrdinaryTransactionResult::PreviousRestored) return recovery;
  if (ops.exists(paths.stage) || ops.exists(paths.backup) ||
      ops.exists(paths.removing)) return OrdinaryTransactionResult::AmbiguousState;
  if (!OrdinaryTransactionDetail::inspect(verify, paths.target, kind, id, observed))
    return OrdinaryTransactionResult::InvalidInstalled;
  if (!ops.rename(paths.target, paths.removing))
    return OrdinaryTransactionResult::RenameFailed;
  if (!purge(paths.removing)) return OrdinaryTransactionResult::RemovalPending;
  observed = {};
  return OrdinaryTransactionResult::Removed;
}

} // namespace RuntimePackages
