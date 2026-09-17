#include "PackageDeviceInstaller.h"

#include <HalStorage.h>

#include <cstdint>
#include <cstring>
#include <mutex>

namespace RuntimePackages {
namespace {
std::mutex& intakePipelineMutex() {
  static std::mutex mutex;
  return mutex;
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
  // The fixed .intake/.extract filenames are global, so serialize the entire
  // operation, not merely the final per-package rename transaction.
  std::lock_guard<std::mutex> lock(intakePipelineMutex());
  approved = {};
  observed = {};
  uint8_t authenticatedPrefix[32]{};
  outcome.intake = stageSignedDevicePackage(source, signers, signerCount,
      policy, resolveCapability, resolverContext, workspace, approved, limits,
      allowFirstInstall, authenticatedPrefix);
  if (outcome.intake != ArchiveStageResult::ReadyForPublicationReview) {
    outcome.result = SignedInstallResult::IntakeRejected;
    return outcome;
  }
  outcome.extraction = extractSignedDevicePackage(authenticatedPrefix,
      signers, signerCount, policy, resolveCapability, resolverContext,
      workspace, observed, limits, allowFirstInstall);
  if (outcome.extraction != ArchiveExtractResult::ReadyForPublicationReview) {
    outcome.result = SignedInstallResult::ExtractionRejected;
    return outcome;
  }
  // Extraction validates the signed prefix, but the exact authenticated
  // generation is checked once more under an exclusive target replacement
  // lease, after it has been renamed to its final managed directory.
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
  // A successfully published, independently reverified generation no longer
  // needs its disposable signed intake. Only delete our fixed intake file;
  // removal failure is explicit and blocks the next intake until recovered.
  if (!Storage.remove(kPackageIntakeStage)) {
    outcome.result = SignedInstallResult::InstalledIntakeCleanupPending;
    return outcome;
  }
  outcome.result = SignedInstallResult::Installed;
  return outcome;
}

} // namespace RuntimePackages
