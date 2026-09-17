#pragma once

#include "PackageDeviceStage.h"
#include "PackageDeviceExtract.h"
#include "PackageDevicePublication.h"

#include <cstdio>

namespace RuntimePackages {

enum class SignedInstallResult : uint8_t {
  Installed, InvalidInput, IntakeRejected, ExtractionRejected,
  PublicationRejected, PublicationPendingRecovery, InstalledIntakeCleanupPending,
  // An existing disposable stage is not authenticated as the exact candidate
  // selected by the caller. Preserve it for explicit inspection/quarantine.
  StaleOrForeignStage
};

struct SignedInstallOutcome {
  SignedInstallResult result = SignedInstallResult::InvalidInput;
  ArchiveStageResult intake = ArchiveStageResult::InvalidInput;
  ArchiveExtractResult extraction = ArchiveExtractResult::InvalidInput;
  SignedTransactionResult publication = SignedTransactionResult::InvalidInput;
};

// One transport-independent runtime-owned pipeline for an already opened
// package FILE*: both offline SD and an online downloader use identical signed
// bytes, signer policy, extraction, NVS and publication checks. The caller
// owns source, signer allowlist and the two distinct large output archives;
// neither archive nor the >8 KiB verification workspace belongs on a small
// FreeRTOS task stack. No signing key is provided by a package.
//
// A trusted caller must explicitly authorize first installs, storage, consent
// and declared dependencies; the manifest itself grants no rights. If an
// interrupted intake exists, reauthenticate it against the caller-selected
// signed source and resume only the exact same fingerprint. A foreign or
// partially written stage is NEVER overwritten, removed or silently adopted.
// If publication reached the final directory before reset, authenticate and
// complete its recovery before removing the matching disposable intake.
//
// This entry point does NOT activate, dlopen or grant hardware capabilities.
// Do not make published SD executable until a byte-bound load gate exists.
SignedInstallOutcome installSignedDevicePackage(std::FILE* source,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& approved, PackageArchive& observed,
    PackageArchiveLimits limits = {}, bool allowFirstInstall = false,
    bool allowSemverDowngrade = false);

} // namespace RuntimePackages
