#pragma once
#include "PackageOrdinaryInstaller.h"
#include "PackageOrdinaryManifest.h"

namespace RuntimePackages {

// The same entrypoint accepts offline SD and downloaded Source objects.
// Manifest bytes are immutable throughout this call. Only a real manager
// holding the per-identity operation lock may invoke this interface.
// The verifier is independently supplied by the storage backend: it MUST
// read the retained .package.json and rehash its exact file inventory.
template <typename Source, typename Destination, typename Hash,
          typename Resolver, typename Ops, typename Verify, typename Purge>
OrdinaryInstallOutcome installCanonicalOrdinaryPackage(
    const char* manifest, size_t manifestBytes, Source& source,
    Destination& destination, Hash& hash, Resolver resolver,
    const PackageRuntimePolicy& policy, uint8_t (&io)[kOrdinaryIoBytes],
    Ops& ops, Verify verifyDirectory, Purge purgeManagedBackup,
    bool replacementAllowed) {
  OrdinaryPackagePlan plan{};
  if (!parseOrdinaryManifest(manifest, manifestBytes, plan)) return {};
  return installOrdinaryPackage(plan,
      reinterpret_cast<const uint8_t*>(manifest), manifestBytes,
      source, destination, hash, resolver, policy, io,
      ops, verifyDirectory, purgeManagedBackup, replacementAllowed);
}

// Directory::readManifest(out, capacity, used) reads its retained manifest,
// bounded by the caller's 4096-byte buffer. No caller-supplied identity is
// trusted during verification; even the version and file list come from disk.
// The caller must additionally verify expected kind and ID against its
// manager-derived path at the transaction boundary.
template <typename Directory, typename Hash, typename Resolver>
bool verifyCanonicalOrdinaryDirectory(Directory& directory, Hash& hash,
    Resolver resolver, const PackageRuntimePolicy& policy,
    uint8_t (&io)[kOrdinaryIoBytes], Identity& observed) {
  observed = {};
  char manifest[PackageJsonGuard::kMaxBytes]{};
  size_t used = 0;
  if (!directory.readManifest(manifest, sizeof(manifest), used) ||
      !used || used > sizeof(manifest)) return false;
  OrdinaryPackagePlan plan{};
  if (!parseOrdinaryManifest(manifest, used, plan) ||
      !verifyOrdinaryDirectory(plan, directory, hash, resolver, policy, io))
    return false;
  observed = plan.identity;
  return true;
}

} // namespace RuntimePackages
