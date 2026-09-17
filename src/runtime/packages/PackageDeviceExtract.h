#pragma once

#include "PackageArchiveExtract.h"
#include "PackageDeviceStage.h"

namespace RuntimePackages {

// Reserved runtime-owned disposable extraction destination. Never an active
// application or driver directory; recovery must explicitly inspect/remove an
// interrupted stage, not delete it automatically at the next install attempt.
constexpr const char* kPackageExtractStage = "/Packages/.extract.part";

// Extract an already staged /Packages/.intake.part into kPackageExtractStage.
// The expected signed-prefix digest MUST have been returned by the successful
// stageSignedDevicePackage call and retained in privileged RAM. This routine
// authenticates intake, enforces runtime and NVS floor policy, hashes and
// rereads every extracted file, and reauthenticates intake. It never publishes,
// loads, activates, grants a capability or advances the persistent floor.
//
// Successful extraction alone does not provide an immutable ELF: the resulting
// files remain on removable SD. Publication must retain verifiable signed
// provenance, survive reset, and close the verify-to-dlopen substitution gap.
ArchiveExtractResult extractSignedDevicePackage(
    const uint8_t expectedSignedPrefixDigest[32],
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& result, PackageArchiveLimits limits = {},
    bool allowFirstInstall = false);

} // namespace RuntimePackages
