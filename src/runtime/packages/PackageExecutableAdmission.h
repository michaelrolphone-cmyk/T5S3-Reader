#pragma once
#include <cstddef>
#include <cstdint>

#include "../../../lib/hal/StorageGeneration.h"
#include "PackageIdentity.h"
namespace RuntimePackages {
struct OrdinaryPackagePlan;
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
                                      const StorageGenerationStamp& sourceStamp);
}  // namespace RuntimePackages
