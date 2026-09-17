#pragma once

#include "PackageDeviceCrypto.h"
#include "PackageDeviceInspection.h"
#include "PackageSignedProvenance.h"

namespace RuntimePackages {

// Read-only verification of an extracted stage, installed generation or its
// recoverable previous generation. Caller supplies expected kind and package
// ID separately; neither the directory path nor an SD record may select its
// own authorization scope. Only manager-derived paths are accepted.
//
// Verifies firmware trust policy, exact file inventory, every signed entry,
// runtime ABI/dependencies and persistent NVS floor. For recovery after a
// power cut BEFORE first-install floor advancement, a privileged manager may
// explicitly authorize first install; the default remains fail closed.
//
// A verified removable-SD directory is NOT immutable through dlopen. Every
// executable loader needs a separate authenticated-byte lifetime boundary.
ProvenanceResult verifySignedDeviceDirectory(const char* storagePath,
    Kind expectedKind, const char* expectedId,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy, PackageCapabilityApi resolveCapability,
    void* resolverContext, PackageVerificationWorkspace& workspace,
    PackageArchive& result, const uint8_t* expectedSignedPrefixDigest = nullptr,
    PackageArchiveLimits limits = {}, bool allowFirstInstall = false);

} // namespace RuntimePackages
