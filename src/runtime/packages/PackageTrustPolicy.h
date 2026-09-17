#pragma once

#include "PackageArchive.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimePackages {

// Authority comes from firmware/device-owned policy only. Never construct
// this array from package metadata or from files on removable storage.
struct TrustedPackageSigner {
  uint32_t id;
  const uint8_t* publicPoint;   // SEC1 uncompressed P-256, exactly 65 bytes.
  size_t publicPointLength;
  Kind allowedKind;
  const char* allowedPackageId; // Exact ID, not a broad prefix or wildcard.
  uint32_t minimumSecurityVersion;
  bool revoked;
};

// Pure policy gate; signature mathematics is separately performed by mbedTLS.
// Duplicate key IDs anywhere in the allowlist fail closed, including a revoked
// collision that could otherwise be shadowed by an active entry.
inline const TrustedPackageSigner* selectTrustedPackageSigner(
    const PackageArchive& archive, uint32_t keyId,
    const TrustedPackageSigner* signers, size_t count) {
  if (!signers || !count || count > 16 || !keyId ||
      archive.signatureAlgorithm != kPackageSignatureP256Sha256 ||
      !safeId(archive.identity.id)) return nullptr;
  const TrustedPackageSigner* selected = nullptr;
  for (size_t i = 0; i < count; ++i) {
    const auto& candidate = signers[i];
    if (!candidate.id) return nullptr;
    for (size_t j = 0; j < i; ++j)
      if (signers[j].id == candidate.id) return nullptr;
    if (candidate.id == keyId) selected = &candidate;
  }
  if (!selected || selected->revoked || !selected->allowedPackageId ||
      !safeId(selected->allowedPackageId) ||
      selected->allowedKind != archive.identity.kind ||
      std::strcmp(selected->allowedPackageId, archive.identity.id) != 0 ||
      !selected->minimumSecurityVersion ||
      archive.securityVersion < selected->minimumSecurityVersion ||
      !selected->publicPoint || selected->publicPointLength != 65 ||
      selected->publicPoint[0] != 0x04) return nullptr;
  return selected;
}

} // namespace RuntimePackages
