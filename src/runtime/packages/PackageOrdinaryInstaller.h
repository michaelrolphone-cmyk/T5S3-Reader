#pragma once

#include "PackageOrdinaryStage.h"
#include "PackageOrdinaryTransaction.h"

namespace RuntimePackages {

// A single manager-only SD/download entrypoint for all package kinds. The
// source and destination implement the bounded stream interfaces in
// PackageOrdinaryStage. Verify MUST independently parse each directory's
// retained manifest, establish its Identity and hash its exact file inventory.
// The previously installed generation has its OWN manifest and version.
// Neither metadata nor an integrity hash grants execution or hardware rights.
enum class OrdinaryInstallResult : uint8_t {
  Installed, InvalidInput, RecoveryRejected, StageAlreadyExists,
  StageRejected, StageVerificationRejected, PublicationRejected,
  PublicationCleanupPending
};
struct OrdinaryInstallOutcome {
  OrdinaryInstallResult result = OrdinaryInstallResult::InvalidInput;
  OrdinaryStageResult staging = OrdinaryStageResult::InvalidInput;
  OrdinaryTransactionResult transaction = OrdinaryTransactionResult::InvalidIdentity;
};

// Caller owns source, parser, manager-derived temporary destination and a
// per-stage serialization lock. Typed transaction code independently derives
// all four-kind paths, checks semantic versions, takes a target replacement
// lease and preserves the previous verified generation on interrupted updates.
// This function never performs dlopen, activation, permission grants or UI.
template <typename Source, typename Destination, typename Hash,
          typename Resolver, typename Ops, typename Verify, typename Purge>
OrdinaryInstallOutcome installOrdinaryPackage(
    const OrdinaryPackagePlan& plan, const uint8_t* manifest,
    size_t manifestBytes, Source& source, Destination& destination, Hash& hash,
    Resolver resolver, const PackageRuntimePolicy& policy,
    uint8_t (&io)[kOrdinaryIoBytes], Ops& ops, Verify verifyDirectory,
    Purge purgeManagedBackup, bool replacementAllowed,
    bool allowDowngrade = false) {
  OrdinaryInstallOutcome outcome{};
  OrdinaryTransactionPaths paths{};
  if (!replacementAllowed || !ordinaryTransactionPaths(plan.identity.kind,
                                                      plan.identity.id, paths))
    return outcome;

  Identity observed{};
  outcome.transaction = recoverOrdinaryPackage(ops, plan.identity.kind,
      plan.identity.id, verifyDirectory, purgeManagedBackup, observed);
  if (outcome.transaction != OrdinaryTransactionResult::NoInstalledPackage &&
      outcome.transaction != OrdinaryTransactionResult::InstalledVerified &&
      outcome.transaction != OrdinaryTransactionResult::PreviousRestored &&
      outcome.transaction != OrdinaryTransactionResult::Removed) {
    outcome.result = OrdinaryInstallResult::RecoveryRejected;
    return outcome;
  }
  if (ops.exists(paths.stage)) {
    outcome.result = OrdinaryInstallResult::StageAlreadyExists;
    return outcome;
  }
  outcome.staging = stageOrdinaryPackage(plan, manifest, manifestBytes, source,
      destination, hash, resolver, policy, io);
  if (outcome.staging != OrdinaryStageResult::ReadyForPublicationReview) {
    outcome.result = OrdinaryInstallResult::StageRejected;
    return outcome;
  }
  observed = {};
  if (!verifyDirectory(paths.stage, observed) ||
      !samePackage(plan.identity, observed) ||
      std::strcmp(plan.identity.version, observed.version) != 0 ||
      std::strcmp(plan.identity.artifact, observed.artifact) != 0) {
    // Discard only files created by this invocation, never an earlier stage
    // or an unknown file in the target/backup.
    (void)destination.discard();
    outcome.result = OrdinaryInstallResult::StageVerificationRejected;
    return outcome;
  }
  outcome.transaction = publishOrdinaryPackage(ops, plan.identity,
      verifyDirectory, purgeManagedBackup, observed, allowDowngrade);
  if (outcome.transaction == OrdinaryTransactionResult::CleanupPending) {
    // Newly published target is verified, but recovery must finish deleting
    // the previous managed generation before another package operation.
    outcome.result = OrdinaryInstallResult::PublicationCleanupPending;
    return outcome;
  }
  if (outcome.transaction != OrdinaryTransactionResult::Published) {
    outcome.result = OrdinaryInstallResult::PublicationRejected;
    return outcome;
  }
  outcome.result = OrdinaryInstallResult::Installed;
  return outcome;
}

} // namespace RuntimePackages
