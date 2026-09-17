#pragma once

#include "PackageArchive.h"

#include <mbedtls/sha256.h>
#include <cstddef>
#include <cstdint>

namespace RuntimePackages {

// Firmware-owned only. An SD manifest may name a key ID, but it cannot add a
// key, change a scope, revoke a revocation, or lower a security floor.
struct TrustedPackageSigner {
  uint32_t id;
  const uint8_t* publicPoint;   // Exactly 65-byte SEC1 uncompressed P-256 point.
  size_t publicPointLength;
  Kind allowedKind;
  const char* allowedPackageId; // Exact package ID, never a filename or wildcard.
  uint32_t minimumSecurityVersion;
  bool revoked;
};

class PackageMbedtlsSha256 {
 public:
  PackageMbedtlsSha256() { mbedtls_sha256_init(&context_); }
  ~PackageMbedtlsSha256() { mbedtls_sha256_free(&context_); }
  PackageMbedtlsSha256(const PackageMbedtlsSha256&) = delete;
  PackageMbedtlsSha256& operator=(const PackageMbedtlsSha256&) = delete;
  bool start() { return mbedtls_sha256_starts_ret(&context_, 0) == 0; }
  bool update(const uint8_t* data, size_t length) {
    return mbedtls_sha256_update_ret(&context_, data, length) == 0;
  }
  bool finish(uint8_t digest[32]) { return mbedtls_sha256_finish_ret(&context_, digest) == 0; }
 private:
  mbedtls_sha256_context context_{};
};

// A verifier is configured by trusted firmware with an allowlist. Passing a
// nullptr or zero keys deliberately authenticates nothing. This adapter does
// not persist rollback floors or authorize installation; both remain manager
// responsibilities outside the signature primitive.
class PackageDeviceTrustVerifier {
 public:
  PackageDeviceTrustVerifier(const PackageArchive& archive,
                             const TrustedPackageSigner* signers, size_t count)
      : archive_(archive), signers_(signers), count_(count) {}
  bool operator()(uint32_t keyId, const uint8_t digest[32],
                  const uint8_t signature[64]) const;
 private:
  const PackageArchive& archive_;
  const TrustedPackageSigner* signers_;
  size_t count_;
};

} // namespace RuntimePackages
