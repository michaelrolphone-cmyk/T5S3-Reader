#include "PackageDeviceProviderProfile.h"
#include "PackageDeviceCrypto.h"
#include "PackageDeviceSecurityFloor.h"

#include <climits>
#include <cstdint>
#include <cstdio>

namespace RuntimePackages {

ProviderProfileResult inspectSignedDeviceProviderProfile(std::FILE* source,
    const uint8_t expectedSignedPrefixDigest[32],
    const TrustedPackageSigner* firmwareSigners, size_t signerCount,
    const PackageRuntimePolicy& policy,
    PackageCapabilityApi resolveCapability, void* resolverContext,
    PackageVerificationWorkspace& workspace, PackageArchive& archive,
    ProviderProfileReceipt& receipt, PackageArchiveLimits limits) {
  archive = {};
  receipt = {};
  constexpr uint64_t overhead = kPackageHeaderBytes + kPackageManifestLimit +
      kPackageSignatureBytes;
  if (!source || !expectedSignedPrefixDigest || !firmwareSigners ||
      !signerCount || signerCount > 16 || !policy.architecture ||
      !policy.runtimeApi || !limits.maxTotalBytes ||
      limits.maxTotalBytes > static_cast<uint64_t>(LONG_MAX) - overhead)
    return ProviderProfileResult::InvalidInput;
  if (std::fseek(source, 0, SEEK_END) != 0)
    return ProviderProfileResult::EntryReadFailure;
  const long measured = std::ftell(source);
  if (measured < static_cast<long>(kPackageHeaderBytes) ||
      static_cast<uint64_t>(measured) > limits.maxTotalBytes + overhead)
    return ProviderProfileResult::InvalidInput;
  const uint64_t length = static_cast<uint64_t>(measured);
  auto readAt = [source, length](uint64_t offset, uint8_t* out, size_t count) {
    return out && offset <= length && count <= length - offset &&
           offset <= static_cast<uint64_t>(LONG_MAX) &&
           std::fseek(source, static_cast<long>(offset), SEEK_SET) == 0 &&
           std::fread(out, 1, count, source) == count;
  };
  PackageMbedtlsSha256 hash;
  PackageDeviceTrustVerifier signer(archive, firmwareSigners, signerCount);
  const auto resolver = [resolveCapability, resolverContext](const char* capability) {
    return resolveCapability ? resolveCapability(capability, resolverContext) : 0u;
  };
  // An unestablished floor does NOT implicitly authorize first installation
  // or provider execution; that decision belongs to explicit manager policy.
  const auto floor = [](const PackageArchive& candidate) {
    return checkPackageSecurityFloor(devicePackageSecurityFloors(),
                                     candidate, false) == FloorCheck::Allowed;
  };
  return verifySignedProviderProfile(readAt, length, expectedSignedPrefixDigest,
      hash, signer, resolver, floor, policy, archive, workspace, receipt, limits);
}

} // namespace RuntimePackages
