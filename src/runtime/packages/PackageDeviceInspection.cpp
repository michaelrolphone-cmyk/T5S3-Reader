#include "PackageDeviceInspection.h"

#include <climits>
#include <cstdint>
#include <cstdio>

namespace RuntimePackages {

PackageInspectionResult inspectSignedPackage(std::FILE* file,
    const TrustedPackageSigner* signers, size_t signerCount,
    const PackageRuntimePolicy& policy,
    PackageCapabilityApi resolveCapability, void* resolverContext,
    PackageVerificationWorkspace& workspace, PackageArchive& result,
    PackageArchiveLimits limits) {
  result = {};
  if (!file || !signers || !signerCount || signerCount > 16 ||
      !policy.architecture || !policy.runtimeApi || !limits.maxTotalBytes ||
      limits.maxTotalBytes > static_cast<uint64_t>(LONG_MAX) -
          (kPackageHeaderBytes + kPackageManifestLimit + kPackageSignatureBytes))
    return PackageInspectionResult::InvalidInput;
  if (std::fseek(file, 0, SEEK_END) != 0) return PackageInspectionResult::UnreadableArchive;
  const long measured = std::ftell(file);
  if (measured < static_cast<long>(kPackageHeaderBytes) ||
      static_cast<uint64_t>(measured) > limits.maxTotalBytes +
          kPackageHeaderBytes + kPackageManifestLimit + kPackageSignatureBytes)
    return PackageInspectionResult::UnreadableArchive;
  const uint64_t length = static_cast<uint64_t>(measured);
  auto readAt = [file, length](uint64_t offset, uint8_t* dest, size_t count) {
    if (offset > length || count > length - offset ||
        offset > static_cast<uint64_t>(LONG_MAX)) return false;
    return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0 &&
        std::fread(dest, 1, count, file) == count;
  };
  PackageMbedtlsSha256 hash;
  PackageDeviceTrustVerifier verify(result, signers, signerCount);
  if (verifyPackageArchive(readAt, length, result, workspace, hash, verify, limits) !=
      PackageVerifyResult::AuthenticatedContent) {
    result = {};
    return PackageInspectionResult::AuthenticationFailed;
  }
  const auto preflight = preflightArchive(result, policy,
      [resolveCapability, resolverContext](const char* capability) -> uint32_t {
        return resolveCapability ? resolveCapability(capability, resolverContext) : 0;
      });
  if (preflight != PreflightResult::ReadyForContentVerification) {
    result = {};
    return PackageInspectionResult::PolicyRejected;
  }
  return PackageInspectionResult::ContentVerifiedForInspection;
}

} // namespace RuntimePackages
