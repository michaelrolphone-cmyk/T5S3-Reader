#pragma once

#include "PackageDeviceInspection.h"
#include "PackageProviderProfile.h"

#include <cstdio>

namespace RuntimePackages {

// Firmware/privileged package-manager call ONLY: check one already opened
// archive against firmware-owned P-256 signers, runtime/dependency policy,
// device-owned NVS rollback floor and an earlier trusted intake fingerprint.
// This is inspection, NOT graph registration, activation or a capability grant.
// An external pointer to a custom key list is never itself a trust root; the
// manager must obtain the allowlist from firmware-owned provisioning policy.
// `receipt` must outlive its consumer and be allocated in manager-owned RAM.
ProviderProfileResult inspectSignedDeviceProviderProfile(std::FILE* source,
    const uint8_t expectedSignedPrefixDigest[32],
    const TrustedPackageSigner* firmwareSigners, size_t signerCount,
    const PackageRuntimePolicy& policy,
    PackageCapabilityApi resolveCapability, void* resolverContext,
    PackageVerificationWorkspace& workspace, PackageArchive& archive,
    ProviderProfileReceipt& receipt, PackageArchiveLimits limits = {});

} // namespace RuntimePackages
