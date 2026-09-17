#pragma once

#include "PackageDeviceDirectory.h"
#include "PackageSignedTransaction.h"

namespace RuntimePackages {

// Privileged manager-only API. The approved archive/fingerprint are produced
// by authenticated intake and extraction; both source types use this exact
// publisher. The target is derived from kind+ID, never chosen by a package.
// The caller must independently authorize storage/consent and signer policy.
// This does NOT activate packages, grant capabilities or authorize ELF loading.
SignedTransactionResult publishSignedDevicePackage(const PackageArchive& approved,
    const uint8_t expectedSignedPrefixDigest[32],
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& observed, PackageArchiveLimits limits = {},
    bool allowFirstInstall = false, bool allowSemverDowngrade = false);

// Boot/inventory recovery must complete before a managed signed generation can
// be considered available. A first-install floor absence needs an explicit
// trusted recovery decision; ordinary NVS is not tamper-resistant storage.
SignedTransactionResult recoverSignedDevicePackage(Kind kind, const char* id,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& observed, PackageArchiveLimits limits = {},
    bool allowFirstInstall = false);

} // namespace RuntimePackages
