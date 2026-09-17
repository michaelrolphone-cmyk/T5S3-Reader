#pragma once

#include "PackageDeviceInstaller.h"

namespace RuntimePackages {

// Firmware-only developer enrollment. No public signing key, signer scope or
// authorization setting is accepted from the removable SD or an application.
// No local header => disabled; merge and boot without an enrollment key safely.
struct MvpFirmwareTrust {
  const TrustedPackageSigner* signers = nullptr;
  size_t signerCount = 0;
  PackageRuntimePolicy policy{"xtensa-esp32s3", 2u, 1u,
                               1024u * 1024u, 4u * 1024u * 1024u};
  PackageCapabilityApi resolveCapability = nullptr;
  void* resolverContext = nullptr;
  bool allowFirstInstall = false;
};

} // namespace RuntimePackages

// Generated locally from the OFF-REPOSITORY test signer public key, with an
// exact kind/ID scope. This is intentionally not a remote or SD config file.
// It defines RuntimePackages::localMvpPackageTrust() returning MvpFirmwareTrust.
#if __has_include(<RiscRteLocalPackageTrust.h>)
#include <RiscRteLocalPackageTrust.h>
#define RISCRTE_HAS_LOCAL_PACKAGE_TRUST 1
#else
#define RISCRTE_HAS_LOCAL_PACKAGE_TRUST 0
#endif

namespace RuntimePackages {
inline MvpFirmwareTrust firmwareMvpPackageTrust() {
#if RISCRTE_HAS_LOCAL_PACKAGE_TRUST
  return localMvpPackageTrust();
#else
  return {};
#endif
}
inline bool usableMvpPackageTrust(const MvpFirmwareTrust& trust) {
  if (!trust.signers || !trust.signerCount || trust.signerCount > 16 ||
      !trust.policy.architecture || !trust.policy.runtimeApi ||
      !trust.policy.maxEntryBytes || !trust.policy.maxTotalBytes) return false;
  for (size_t i = 0; i < trust.signerCount; ++i) {
    const auto& signer = trust.signers[i];
    if (!signer.id || signer.revoked || !signer.publicPoint ||
        signer.publicPointLength != 65 || signer.publicPoint[0] != 4 ||
        !safeId(signer.allowedPackageId) || !signer.minimumSecurityVersion)
      return false;
    for (size_t j = 0; j < i; ++j)
      if (trust.signers[j].id == signer.id) return false;
  }
  return true;
}
} // namespace RuntimePackages
