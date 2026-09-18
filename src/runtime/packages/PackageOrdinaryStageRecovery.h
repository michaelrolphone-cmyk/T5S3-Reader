#pragma once

#include "PackageOrdinaryTransaction.h"

namespace RuntimePackages {

// Manager-only recovery of a *staged* ordinary package. No operation reads a
// release catalog, activates an ELF, changes permissions or assumes signatures.
// The same kind/ID path derivation applies to applications, drivers, services
// and providers. The caller must serialize operations for this identity.
enum class OrdinaryStageState : uint8_t {
  InvalidIdentity, Missing, RecoveryRequired, InvalidInstalled, InvalidStage,
  StaleVersion, InUse, Ready
};

struct OrdinaryStageReview {
  OrdinaryStageState state = OrdinaryStageState::InvalidIdentity;
  Identity candidate{};
  Identity installed{};
};

// Read-only: never rename, purge or 'repair' a stage merely by inspecting it.
// Verify must independently parse the retained manifest, enumerate exactly the
// files declared by it and hash the actual bytes. An invalid staged directory
// remains untouched for the owner's explicit disposition.
template <typename Ops, typename Verify>
OrdinaryStageReview reviewOrdinaryStage(Ops& ops, Kind kind,
                                        const char* id, Verify verify) {
  OrdinaryStageReview review{};
  OrdinaryTransactionPaths paths{};
  if (!ordinaryTransactionPaths(kind, id, paths)) return review;
  if (ops.exists(paths.backup) || ops.exists(paths.removing)) {
    review.state = OrdinaryStageState::RecoveryRequired;
    return review;
  }
  if (ops.exists(paths.target) &&
      !OrdinaryTransactionDetail::inspect(verify, paths.target, kind, id,
                                          review.installed)) {
    review.state = OrdinaryStageState::InvalidInstalled;
    return review;
  }
  if (!ops.exists(paths.stage)) {
    review.state = OrdinaryStageState::Missing;
    return review;
  }
  if (!OrdinaryTransactionDetail::inspect(verify, paths.stage, kind, id,
                                          review.candidate)) {
    review.state = OrdinaryStageState::InvalidStage;
    return review;
  }
  if (ops.exists(paths.target) &&
      decidePackageVersion(review.candidate, &review.installed) !=
          InstallDecision::Upgrade) {
    review.state = OrdinaryStageState::StaleVersion;
    return review;
  }
  if (systemPackageUseGate().pinned(paths.target)) {
    review.state = OrdinaryStageState::InUse;
    return review;
  }
  review.state = OrdinaryStageState::Ready;
  return review;
}

// Explicit retry does not fetch again and cannot bypass publication's OWN
// exclusive lease/reverification: a loader or another manager can race the
// advisory review. A bad stage or stale version is never rewritten to fit.
template <typename Ops, typename Verify, typename Purge>
OrdinaryTransactionResult retryOrdinaryStage(Ops& ops, Kind kind,
    const char* id, Verify verify, Purge purge, Identity& observed) {
  const OrdinaryStageReview review = reviewOrdinaryStage(ops, kind, id, verify);
  switch (review.state) {
    case OrdinaryStageState::Ready:
      return publishOrdinaryPackage(ops, review.candidate, verify, purge, observed);
    case OrdinaryStageState::InvalidIdentity:
      return OrdinaryTransactionResult::InvalidIdentity;
    case OrdinaryStageState::Missing:
    case OrdinaryStageState::InvalidStage:
      return OrdinaryTransactionResult::InvalidStage;
    case OrdinaryStageState::InvalidInstalled:
      return OrdinaryTransactionResult::InvalidInstalled;
    case OrdinaryStageState::StaleVersion:
      return OrdinaryTransactionResult::VersionRejected;
    case OrdinaryStageState::InUse:
      return OrdinaryTransactionResult::InUse;
    case OrdinaryStageState::RecoveryRequired:
      return OrdinaryTransactionResult::AmbiguousState;
  }
  return OrdinaryTransactionResult::AmbiguousState;
}

enum class OrdinaryStageDiscardResult : uint8_t {
  Discarded, InvalidIdentity, NoStage, RecoveryRequired, InUse,
  UnknownEntries, CleanupFailed
};

// Separate, explicitly invoked discard. inspectKnownEntries MUST reject all
// unknown files, nested directories and links and may allow a known *partial*
// stage (e.g. just driver.elf after a power cut). purgeKnownEntries MUST remove
// only those verified known entries and the manager-owned directory, checking
// every mutation result. Neither callback may touch target, backup or user files.
template <typename Ops, typename InspectKnown, typename PurgeKnown>
OrdinaryStageDiscardResult discardOrdinaryStage(Ops& ops, Kind kind,
    const char* id, InspectKnown inspectKnownEntries,
    PurgeKnown purgeKnownEntries) {
  OrdinaryTransactionPaths paths{};
  if (!ordinaryTransactionPaths(kind, id, paths))
    return OrdinaryStageDiscardResult::InvalidIdentity;
  PackageReplacementLease lease(paths.target);
  if (!lease) return OrdinaryStageDiscardResult::InUse;
  // A power-cut replacement/uninstall must be recovered before altering its
  // stage. Never erase evidence while another generation is unresolved.
  if (ops.exists(paths.backup) || ops.exists(paths.removing))
    return OrdinaryStageDiscardResult::RecoveryRequired;
  if (!ops.exists(paths.stage)) return OrdinaryStageDiscardResult::NoStage;
  if (!inspectKnownEntries(paths.stage))
    return OrdinaryStageDiscardResult::UnknownEntries;
  if (!purgeKnownEntries(paths.stage) || ops.exists(paths.stage))
    return OrdinaryStageDiscardResult::CleanupFailed;
  return OrdinaryStageDiscardResult::Discarded;
}

}  // namespace RuntimePackages
