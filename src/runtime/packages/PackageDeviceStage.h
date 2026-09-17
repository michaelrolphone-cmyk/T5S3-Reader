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

// Inspect source, check the persistent per-kind/ID security floor, copy to a
// fresh exclusive HalStorage stage, close/reopen, authenticate all sealed bytes
// and recheck the floor. Missing floor records fail closed unless the trusted
// caller explicitly authorizes a first installation. Existing intake files
// are preserved and require separate recovery, not destructive cleanup.
// Success retains a stage for separate publication review. This function
// neither commits the security floor nor activates/loads/authorizes anything.
//
// If expectedSignedPrefixDigest is non-null, it MUST point to 32 writable
// manager-owned bytes. Failure zeros it; success records the SHA-256 of the
// authenticated header+manifest for later extraction identity comparison.
// Keep this fingerprint outside mutable SD and do not reconstruct it from the
// intake after an attacker has had a chance to replace that intake.
//
// IMPORTANT: an SD stage can be altered after this function returns. The
// publisher must repeat the floor check and enforce authenticated-byte
// lifetime through publication and ELF loading. NVS does not protect against
// a physical adversary who can rewrite or roll back raw device flash.
ArchiveStageResult stageSignedDevicePackage(std::FILE* source,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& result, PackageArchiveLimits limits = {},
    bool allowFirstInstall = false,
    uint8_t expectedSignedPrefixDigest[32] = nullptr);

} // namespace RuntimePackages
