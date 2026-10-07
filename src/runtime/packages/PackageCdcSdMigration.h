#pragma once
#include "PackageCdcMigration.h"
#include "PackageOrdinaryInstaller.h"
namespace RuntimePackages {
// Mutation functions require the existing shared ScopedPackageMutation gate.
// They take both root leases themselves; callers must not pre-acquire either.
bool cdcMigrationPendingOnSd();
OrdinaryTransactionResult reconcileCdcMigrationFromSd(const PackageRuntimePolicy&, uint32_t (*resolver)(const char*),
                                                      Identity& observed);
OrdinaryTransactionResult recoverCanonicalCdcFromSd(const PackageRuntimePolicy&, uint32_t (*resolver)(const char*),
                                                    Identity& observed);
OrdinaryTransactionResult publishCanonicalCdcFromSd(const Identity&, const PackageRuntimePolicy&,
                                                    uint32_t (*resolver)(const char*), Identity& observed);
// Read-only preview returns the greatest validated installed lineage version.
bool previewCanonicalCdcFromSd(const Identity&, const PackageRuntimePolicy&, uint32_t (*resolver)(const char*),
                               Identity& installed, bool& allowed);
struct OrdinarySdLineageTransaction {
  const PackageRuntimePolicy& policy;
  uint32_t (*resolver)(const char*);
  template <typename Ops, typename Verify, typename Purge>
  OrdinaryTransactionResult recover(Ops& ops, Kind kind, const char* id, Verify verify, Purge purge,
                                    Identity& observed) const {
    if (kind == Kind::Driver && !std::strcmp(id, kCdcAliasId)) return OrdinaryTransactionResult::InvalidIdentity;
    if (kind == Kind::Driver && !std::strcmp(id, kCdcCanonicalId))
      return recoverCanonicalCdcFromSd(policy, resolver, observed);
    return recoverOrdinaryPackage(ops, kind, id, verify, purge, observed);
  }
  template <typename Ops, typename Verify, typename Purge>
  OrdinaryTransactionResult publish(Ops& ops, const Identity& candidate, Verify verify, Purge purge, Identity& observed,
                                    bool downgrade) const {
    if (candidate.kind == Kind::Driver && !std::strcmp(candidate.id, kCdcCanonicalId))
      return publishCanonicalCdcFromSd(candidate, policy, resolver, observed);
    return publishOrdinaryPackage(ops, candidate, verify, purge, observed, downgrade);
  }
};
}  // namespace RuntimePackages
