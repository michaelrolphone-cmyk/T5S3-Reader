#pragma once

#include "PackageArchiveVerification.h"
#include "PackageDeviceCrypto.h"

#include <cstdio>

namespace RuntimePackages {

using PackageCapabilityApi = uint32_t (*)(const char* capability, void* context);

enum class PackageInspectionResult : uint8_t {
  ContentVerifiedForInspection, InvalidInput, UnreadableArchive,
  AuthenticationFailed, PolicyRejected
};

// Device entry point for already-opened, read-only .risc file handles. The
// calling privileged Package Manager must own the mount/storage lease and
// supply trusted runtime policy + firmware signer allowlist. This function
// never writes files, loads code, grants capabilities or installs packages.
// In particular, success does not pin bytes across later SD mutations.
PackageInspectionResult inspectSignedPackage(std::FILE* file,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy,
    PackageCapabilityApi resolveCapability, void* resolverContext,
    PackageVerificationWorkspace& workspace, PackageArchive& result,
    PackageArchiveLimits limits = {});

} // namespace RuntimePackages
