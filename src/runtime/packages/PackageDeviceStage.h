#pragma once

#include "PackageArchiveStage.h"
#include "PackageDeviceCrypto.h"
#include "PackageDeviceInspection.h"

#include <cstdio>

namespace RuntimePackages {

// Fixed, runtime-owned scratch path. Both an offline SD reader and an online
// downloader pass an already-opened readable FILE*, not a package-supplied path.
// Only privileged runtime code can call this entry point. The stage is never
// interpreted as an installed executable merely because it exists on the SD.
constexpr const char* kPackageIntakeStage = "/Packages/.intake.part";

// Inspect source, copy to a fresh exclusive HalStorage stage, close/reopen it,
// and authenticate all sealed bytes again against firmware-owned signer policy.
// Existing files at kPackageIntakeStage are preserved and cause refusal; they
// require explicit recovery, not an implicit destructive cleanup. Success
// retains the stage for a separate, future publication decision. No payload is
// loaded, no package is activated and no capability is granted here.
//
// IMPORTANT: authentication of an SD stage does not guarantee that its bytes
// remain immutable after this function returns. Installation/load MUST enforce
// a separate verified-byte lifetime mechanism before enabling production use.
ArchiveStageResult stageSignedDevicePackage(std::FILE* source,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& result, PackageArchiveLimits limits = {});

} // namespace RuntimePackages
