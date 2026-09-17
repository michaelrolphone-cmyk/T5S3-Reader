#pragma once

#include "PackageDeviceStage.h"
#include "PackageDeviceExtract.h"
#include "PackageDevicePublication.h"

#include <cstdio>

namespace RuntimePackages {

enum class SignedInstallResult : uint8_t {
  Installed, InvalidInput, IntakeRejected, ExtractionRejected,
  PublicationRejected, PublicationPendingRecovery, InstalledIntakeCleanupPending
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
// and declared dependencies; the manifest itself grants no rights. This entry
// point does NOT activate, dlopen or grant hardware capabilities. Do not make
// published SD executable until a byte-bound load gate is implemented.
SignedInstallOutcome installSignedDevicePackage(std::FILE* source,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& approved, PackageArchive& observed,
    PackageArchiveLimits limits = {}, bool allowFirstInstall = false,
    bool allowSemverDowngrade = false);

} // namespace RuntimePackages
