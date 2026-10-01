#pragma once
#include <cstddef>
#include <cstdint>

#include "../../../lib/hal/StorageGeneration.h"
#include "PackageIdentity.h"
namespace RuntimeProviders {
class ModuleV2;
}
namespace RuntimePackages {
struct OrdinaryPackagePlan;
struct PackageRuntimePolicy;
// Preserve full policy checks outside recursive provider-registration frames.
bool preflightCapturedPackage(const OrdinaryPackagePlan& plan, const PackageRuntimePolicy& policy);
// An ephemeral proof for a fresh copy of graph-owned immutable bytes already
// verified during this node lifetime. Only the loader can create it; no SDK or
// manager caller can mark an arbitrary mutable input as verified.
class VerifiedImageCopy final {
 public:
  bool matches(const uint8_t* bytes, size_t size, const uint8_t* digest) const {
    return bytes_ == bytes && size_ == size && digest && !std::memcmp(digest_, digest, 32);
  }

 private:
  friend class RuntimeProviders::ModuleV2;
  VerifiedImageCopy() = default;
  VerifiedImageCopy(const VerifiedImageCopy&) = delete;
  VerifiedImageCopy& operator=(const VerifiedImageCopy&) = delete;
  const uint8_t* bytes_ = nullptr;
  size_t size_ = 0;
  uint8_t digest_[32]{};
};
// Bounded SHA of operation-owned bytes, never a pathname reopen. Used by
// manager admission for small profile/import snapshots and cold ELF mapping.
bool packageSnapshotDigest(const uint8_t* bytes, size_t size, uint8_t (&digest)[32]);
bool declaredPackageSnapshot(const OrdinaryPackagePlan& plan, const char* name, const uint8_t* bytes, size_t size);
// Private loader boundary. Caller retains the installed package-use pin and
// owns the immutable bytes that will be relocated. Manifest/import declarations
// do not grant capability or hardware authority. Warm reuse is current-process
// observed coherence only; persisted receipt bytes never initialize that proof.
bool admitInstalledExecutableSnapshot(const Identity& identity, const uint8_t* expectedManifestDigest,
                                      const uint8_t* expectedExecutableDigest, const uint8_t* snapshot, size_t length,
                                      const StorageGenerationStamp& sourceStamp,
                                      const VerifiedImageCopy* verifiedCopy = nullptr);
}  // namespace RuntimePackages
