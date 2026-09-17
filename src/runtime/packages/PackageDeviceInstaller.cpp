#include "PackageDeviceInstaller.h"
#include "PackageDeviceCrypto.h"
#include "PackageDeviceSecurityFloor.h"

#include <HalStorage.h>

#include <cstdint>
#include <cstring>
#include <mutex>

namespace RuntimePackages {
namespace {
constexpr const char* kIntakeVfsPath = "/sd/Packages/.intake.part";

std::mutex& intakePipelineMutex() {
  static std::mutex mutex;
  return mutex;
}

bool sameFingerprint(const uint8_t lhs[32], const uint8_t rhs[32]) {
  uint8_t difference = 0;
  for (size_t i = 0; i < 32; ++i)
    difference |= static_cast<uint8_t>(lhs[i] ^ rhs[i]);
  return difference == 0;
}

bool fingerprintVerifiedPrefix(const PackageArchive& archive,
    const PackageVerificationWorkspace& workspace, uint8_t digest[32]) {
  if (archive.signatureOffset < kPackageHeaderBytes + 16 ||
      archive.signatureOffset > sizeof(workspace.signedPrefix)) return false;
  PackageMbedtlsSha256 hash;
  return hash.start() &&
      hash.update(workspace.signedPrefix,
                  static_cast<size_t>(archive.signatureOffset)) &&
      hash.finish(digest);
}

bool installedMatchesSelected(const PackageArchive& approved,
    const uint8_t fingerprint[32], const TrustedPackageSigner* signers,
    size_t signerCount, const PackageRuntimePolicy& policy,
    PackageCapabilityApi resolveCapability, void* resolverContext,
    PackageVerificationWorkspace& workspace, PackageArchive& observed,
    PackageArchiveLimits limits, bool allowFirstInstall) {
  SignedTransactionPaths paths{};
  return signedTransactionPaths(approved.identity.kind, approved.identity.id,
                                paths) && Storage.exists(paths.target) &&
      verifySignedDeviceDirectory(paths.target, approved.identity.kind,
          approved.identity.id, signers, signerCount, policy, resolveCapability,
          resolverContext, workspace, observed, fingerprint, limits,
          allowFirstInstall) == ProvenanceResult::AuthenticatedDirectory;
}

} // namespace

SignedInstallOutcome installSignedDevicePackage(std::FILE* source,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& approved, PackageArchive& observed,
    PackageArchiveLimits limits, bool allowFirstInstall,
    bool allowSemverDowngrade) {
  SignedInstallOutcome outcome{};
  if (!source || !signers || !signerCount || signerCount > 16 ||
      !policy.architecture || !policy.runtimeApi ||
      &approved == &observed || !Storage.ready()) return outcome;

  // The fixed .intake/.extract filenames are global. Serialize the entire
  // manager operation rather than merely a target-specific rename. Public
  // callers must still acquire the storage-volume lifetime lease externally.
  std::lock_guard<std::mutex> lock(intakePipelineMutex());
  approved = {};
  observed = {};
  uint8_t authenticatedPrefix[32]{};
  const bool pendingIntake = Storage.exists(kPackageIntakeStage);
  const bool pendingExtract = Storage.exists(kPackageExtractStage);

  if (pendingIntake) {
    // The selected, already-open source is the authority for what the user
    // requested to resume. An unrelated, truncated or malicious leftover
    // intake cannot be silently adopted, overwritten or deleted.
    if (inspectSignedPackage(source, signers, signerCount, policy,
          resolveCapability, resolverContext, workspace, approved, limits) !=
            PackageInspectionResult::ContentVerifiedForInspection ||
        checkPackageSecurityFloor(devicePackageSecurityFloors(), approved,
                                  allowFirstInstall) != FloorCheck::Allowed ||
        !fingerprintVerifiedPrefix(approved, workspace, authenticatedPrefix)) {
      approved = {};
      outcome.result = SignedInstallResult::IntakeRejected;
      return outcome;
    }
    std::FILE* intake = std::fopen(kIntakeVfsPath, "rb");
    if (!intake) {
      approved = {};
      outcome.result = SignedInstallResult::StaleOrForeignStage;
      return outcome;
    }
    const bool intakeVerified = inspectSignedPackage(intake, signers, signerCount,
        policy, resolveCapability, resolverContext, workspace, observed, limits) ==
        PackageInspectionResult::ContentVerifiedForInspection;
    uint8_t intakeFingerprint[32]{};
    const bool matches = intakeVerified &&
        fingerprintVerifiedPrefix(observed, workspace, intakeFingerprint) &&
        sameFingerprint(authenticatedPrefix, intakeFingerprint) &&
        observed.identity.kind == approved.identity.kind &&
        std::strcmp(observed.identity.id, approved.identity.id) == 0;
    const bool closed = std::fclose(intake) == 0;
    if (!matches || !closed) {
      approved = {};
      observed = {};
      outcome.result = SignedInstallResult::StaleOrForeignStage;
      return outcome;
    }
    outcome.intake = ArchiveStageResult::ReadyForPublicationReview;
  } else if (pendingExtract) {
    // A recovered extraction without its authenticated intake cannot prove
    // that it belongs to the caller-selected operation. Never adopt it.
    outcome.result = SignedInstallResult::StaleOrForeignStage;
    return outcome;
  } else {
    outcome.intake = stageSignedDevicePackage(source, signers, signerCount,
        policy, resolveCapability, resolverContext, workspace, approved, limits,
        allowFirstInstall, authenticatedPrefix);
    if (outcome.intake != ArchiveStageResult::ReadyForPublicationReview) {
      outcome.result = SignedInstallResult::IntakeRejected;
      return outcome;
    }
  }

  if (pendingIntake && !pendingExtract) {
    // A reset may have occurred after stage -> target, backup cleanup or an
    // NVS commit, but before intake removal. Reconcile the installed signed
    // generation first. The same signed prefix then makes retry idempotent.
    outcome.publication = recoverSignedDevicePackage(approved.identity.kind,
        approved.identity.id, signers, signerCount, policy, resolveCapability,
        resolverContext, workspace, observed, limits, allowFirstInstall);
    if (outcome.publication == SignedTransactionResult::BackupCleanupPending ||
        outcome.publication == SignedTransactionResult::FloorCommitPending ||
        outcome.publication == SignedTransactionResult::RestoreFailed) {
      outcome.result = SignedInstallResult::PublicationPendingRecovery;
      return outcome;
    }
    if (outcome.publication != SignedTransactionResult::NoInstalledGeneration &&
        outcome.publication != SignedTransactionResult::RecoveredGeneration &&
        outcome.publication != SignedTransactionResult::VerifiedGeneration) {
      outcome.result = SignedInstallResult::PublicationRejected;
      return outcome;
    }
    if (installedMatchesSelected(approved, authenticatedPrefix, signers,
          signerCount, policy, resolveCapability, resolverContext, workspace,
          observed, limits, allowFirstInstall)) {
      if (!Storage.remove(kPackageIntakeStage)) {
        outcome.result = SignedInstallResult::InstalledIntakeCleanupPending;
        return outcome;
      }
      outcome.result = SignedInstallResult::Installed;
      return outcome;
    }
  }

  if (pendingExtract) {
    // The extracted directory is independently reauthenticated from its
    // retained signer provenance, full inventory and payloads. A matching
    // intake name alone is not sufficient authorization for a resume.
    if (verifySignedDeviceDirectory(kPackageExtractStage,
          approved.identity.kind, approved.identity.id, signers, signerCount,
          policy, resolveCapability, resolverContext, workspace, observed,
          authenticatedPrefix, limits, allowFirstInstall) !=
            ProvenanceResult::AuthenticatedDirectory) {
      outcome.result = SignedInstallResult::StaleOrForeignStage;
      return outcome;
    }
    outcome.extraction = ArchiveExtractResult::ReadyForPublicationReview;
  } else {
    outcome.extraction = extractSignedDevicePackage(authenticatedPrefix,
        signers, signerCount, policy, resolveCapability, resolverContext,
        workspace, observed, limits, allowFirstInstall);
    if (outcome.extraction != ArchiveExtractResult::ReadyForPublicationReview) {
      outcome.result = SignedInstallResult::ExtractionRejected;
      return outcome;
    }
  }

  // Publication holds a target-exclusive replacement lease, checks the same
  // authenticated prefix again and advances NVS only after safe backup purge.
  outcome.publication = publishSignedDevicePackage(approved,
      authenticatedPrefix, signers, signerCount, policy, resolveCapability,
      resolverContext, workspace, observed, limits, allowFirstInstall,
      allowSemverDowngrade);
  if (outcome.publication == SignedTransactionResult::BackupCleanupPending ||
      outcome.publication == SignedTransactionResult::FloorCommitPending ||
      outcome.publication == SignedTransactionResult::RestoreFailed) {
    outcome.result = SignedInstallResult::PublicationPendingRecovery;
    return outcome;
  }
  if (outcome.publication != SignedTransactionResult::Published) {
    outcome.result = SignedInstallResult::PublicationRejected;
    return outcome;
  }
  // Removal failure must never be called a successful, fully cleaned install;
  // an exact-source retry above authenticates the target and removes intake.
  if (!Storage.remove(kPackageIntakeStage)) {
    outcome.result = SignedInstallResult::InstalledIntakeCleanupPending;
    return outcome;
  }
  outcome.result = SignedInstallResult::Installed;
  return outcome;
}

} // namespace RuntimePackages
