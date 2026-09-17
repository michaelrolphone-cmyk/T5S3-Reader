#pragma once

#include "PackageOrdinaryStage.h"
#include "PackageTransaction.h"

namespace RuntimePackages {

// Manager-only, transport-independent installation path. Source and stage
// implement the ordinary 512-byte streaming interfaces; Ops owns manager-
// derived target/stage/backup paths. Verify MUST independently parse each
// directory's stored manifest, check its identity and exact inventory, and
// hash all entries. In particular, the old generation has its own version and
// manifest: do not verify it against the incoming candidate's plan.
// Neither package metadata nor a matching checksum authorizes an ELF to load.
enum class OrdinaryInstallResult : uint8_t {
  Installed, InvalidInput, RecoveryRejected, StageAlreadyExists,
  StageRejected, StageVerificationRejected, PublicationRejected
};

struct OrdinaryInstallOutcome {
  OrdinaryInstallResult result = OrdinaryInstallResult::InvalidInput;
  OrdinaryStageResult staging = OrdinaryStageResult::InvalidInput;
};

// The same function handles SD and downloaded Source implementations. It does
// NOT perform UI, network, package parsing, automatic activation or device
// permission grants. Caller serializes transactions for the same stage path;
// PackageTransaction takes the target's exclusive replacement lease during
// recovery and publication. A failed stage may discard only files it created.
template <typename Source, typename Destination, typename Hash,
          typename Resolver, typename Ops, typename Verify, typename Purge>
OrdinaryInstallOutcome installOrdinaryPackage(
    const OrdinaryPackagePlan& plan, const uint8_t* manifest,
    size_t manifestBytes, Source& source, Destination& destination, Hash& hash,
    Resolver resolver, const PackageRuntimePolicy& policy,
    uint8_t (&io)[kOrdinaryIoBytes], Ops& ops, const TransactionPaths& paths,
    Verify verifyDirectory, Purge purgeManagedBackup, bool replacementAllowed) {
  OrdinaryInstallOutcome outcome{};
  if (!replacementAllowed || !paths.target || !paths.stage || !paths.backup ||
      !paths.target[0] || !paths.stage[0] || !paths.backup[0] ||
      std::strcmp(paths.target, paths.stage) == 0 ||
      std::strcmp(paths.target, paths.backup) == 0 ||
      std::strcmp(paths.stage, paths.backup) == 0)
    return outcome;

  // Recover BEFORE staging: never overwrite interrupted operations or erase
  // an unknown directory. The verifier must evaluate the installed generation,
  // not assume its manifest is the version being installed now.
  if (!recoverDirectoryTransaction(ops, paths, verifyDirectory,
                                   purgeManagedBackup)) {
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
  if (!verifyDirectory(paths.stage)) {
    // Destination can dispose of only its own files, not an existing target,
    // backup, unknown file or a stage from another installation.
    (void)destination.discard();
    outcome.result = OrdinaryInstallResult::StageVerificationRejected;
    return outcome;
  }
  if (!publishDirectoryTransaction(ops, paths, verifyDirectory,
                                   purgeManagedBackup, replacementAllowed)) {
    outcome.result = OrdinaryInstallResult::PublicationRejected;
    return outcome;
  }
  outcome.result = OrdinaryInstallResult::Installed;
  return outcome;
}

} // namespace RuntimePackages
