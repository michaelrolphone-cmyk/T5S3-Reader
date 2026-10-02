#pragma once
#include "PackageCdcLineage.h"
#include "PackageOrdinaryTransaction.h"
namespace RuntimePackages {
struct CdcMigrationIntent {
  char candidateVersion[32]{};
  char aliasVersion[32]{};
  uint8_t manifestDigest[32]{};
  uint8_t aliasManifestDigest[32]{};
};
namespace CdcMigrationDetail {
inline bool valid(const CdcMigrationIntent& intent) {
  return safeVersion(intent.candidateVersion) && safeVersion(intent.aliasVersion) &&
         comparePackageVersions(intent.candidateVersion, intent.aliasVersion) == VersionOrder::Newer;
}
template <typename Ops, typename Verify>
bool matches(Ops& ops, Verify& verify, const char* path, const CdcMigrationIntent& intent, Identity& observed) {
  uint8_t digest[32]{};
  return OrdinaryTransactionDetail::inspect(verify, path, Kind::Driver, kCdcCanonicalId, observed) &&
         !std::strcmp(observed.artifact, "driver.elf") && !std::strcmp(observed.version, intent.candidateVersion) &&
         ops.manifestDigest(path, digest) && !std::memcmp(digest, intent.manifestDigest, sizeof(digest));
}
// Both exact-root replacement leases and the manager mutation gate are held.
// An intent binds recovery to the actual staged manifest, never just absence
// of a filename, modtime, or whichever canonical generation happens to exist.
template <typename Ops, typename Verify, typename Purge>
OrdinaryTransactionResult recoverLocked(Ops& ops, Verify verify, Purge purge, Identity& observed) {
  OrdinaryTransactionPaths canonical{}, alias{};
  ordinaryTransactionPaths(Kind::Driver, kCdcCanonicalId, canonical);
  ordinaryTransactionPaths(Kind::Driver, kCdcAliasId, alias);
  CdcMigrationIntent intent{};
  if (!ops.readIntent(intent) || !valid(intent) || ops.exists(canonical.removing) || ops.exists(alias.backup) ||
      ops.exists(alias.removing) || ops.exists(alias.stage))
    return OrdinaryTransactionResult::AmbiguousState;
  const bool held = ops.exists(kCdcHoldingRoot), liveAlias = ops.exists(alias.target);
  if (held && liveAlias) return OrdinaryTransactionResult::AmbiguousState;
  if (!ops.exists(canonical.stage) && matches(ops, verify, canonical.target, intent, observed)) {
    if (liveAlias) return OrdinaryTransactionResult::AmbiguousState;
    // The exact candidate is committed. A partially purged alias is expected;
    // purge independently checks its remaining declared inventory, manifest last.
    const auto result = OrdinaryTransactionDetail::recoverLocked(ops, canonical, Kind::Driver, kCdcCanonicalId, verify,
                                                                 purge, observed);
    if (result != OrdinaryTransactionResult::InstalledVerified && result != OrdinaryTransactionResult::PreviousRestored)
      return result;
    if (held && !ops.purgeHolding(intent)) return OrdinaryTransactionResult::CleanupPending;
    return ops.clearIntent(intent) ? OrdinaryTransactionResult::InstalledVerified
                                   : OrdinaryTransactionResult::CleanupPending;
  }
  // No committed candidate: only its exact retained stage permits rollback.
  if (!ops.exists(canonical.stage) || !matches(ops, verify, canonical.stage, intent, observed) ||
      (ops.exists(canonical.target) && ops.exists(canonical.backup)))
    return OrdinaryTransactionResult::AmbiguousState;
  Identity oldAlias{};
  uint8_t aliasDigest[32]{};
  if ((!held && !liveAlias) ||
      !OrdinaryTransactionDetail::inspect(verify, held ? kCdcHoldingRoot : alias.target, Kind::Driver, kCdcAliasId,
                                          oldAlias) ||
      std::strcmp(oldAlias.version, intent.aliasVersion) ||
      !ops.manifestDigest(held ? kCdcHoldingRoot : alias.target, aliasDigest) ||
      std::memcmp(aliasDigest, intent.aliasManifestDigest, sizeof(aliasDigest)))
    return OrdinaryTransactionResult::InvalidInstalled;
  const auto restored =
      OrdinaryTransactionDetail::recoverLocked(ops, canonical, Kind::Driver, kCdcCanonicalId, verify, purge, observed);
  if (restored != OrdinaryTransactionResult::InstalledVerified &&
      restored != OrdinaryTransactionResult::PreviousRestored &&
      restored != OrdinaryTransactionResult::NoInstalledPackage)
    return restored;
  if (held && !ops.rename(kCdcHoldingRoot, alias.target)) return OrdinaryTransactionResult::RestorePending;
  return ops.clearIntent(intent) ? restored : OrdinaryTransactionResult::RestorePending;
}
}  // namespace CdcMigrationDetail

template <typename Ops, typename Verify, typename Purge>
OrdinaryTransactionResult recoverCdcMigration(Ops& ops, Verify verify, Purge purge, Identity& observed) {
  if (!cdcMigrationPending(ops)) return OrdinaryTransactionResult::NoInstalledPackage;
  PackageReplacementLease canonical(kCdcCanonicalRoot), alias(kCdcAliasRoot);
  if (!canonical || !alias) return OrdinaryTransactionResult::InUse;
  return CdcMigrationDetail::recoverLocked(ops, verify, purge, observed);
}

template <typename Ops, typename Verify, typename Purge>
OrdinaryTransactionResult publishCdcPackage(Ops& ops, const Identity& requested, Verify verify, Purge purge,
                                            Identity& observed) {
  Identity candidate{};
  if (!makeIdentity(requested.kind, requested.id, requested.version, requested.artifact, false, &candidate) ||
      requested.legacyVersion)
    return OrdinaryTransactionResult::InvalidIdentity;
  if (candidate.kind != Kind::Driver || std::strcmp(candidate.id, kCdcCanonicalId) ||
      std::strcmp(candidate.artifact, "driver.elf") || !safeVersion(candidate.version))
    return OrdinaryTransactionResult::InvalidIdentity;
  PackageReplacementLease canonicalLease(kCdcCanonicalRoot), aliasLease(kCdcAliasRoot);
  if (!canonicalLease || !aliasLease) return OrdinaryTransactionResult::InUse;
  if (cdcMigrationPending(ops)) return OrdinaryTransactionResult::AmbiguousState;
  OrdinaryTransactionPaths canonical{}, alias{};
  ordinaryTransactionPaths(Kind::Driver, kCdcCanonicalId, canonical);
  ordinaryTransactionPaths(Kind::Driver, kCdcAliasId, alias);
  if (ops.exists(alias.stage) || ops.exists(alias.backup) || ops.exists(alias.removing) || ops.legacyPending())
    return OrdinaryTransactionResult::AmbiguousState;
  if (!ops.exists(alias.target))
    return OrdinaryTransactionDetail::publishLocked(ops, candidate, verify, purge, observed, false);
  Identity oldAlias{}, staged{};
  if (!OrdinaryTransactionDetail::inspect(verify, alias.target, Kind::Driver, kCdcAliasId, oldAlias))
    return OrdinaryTransactionResult::InvalidInstalled;
  if (comparePackageVersions(candidate.version, oldAlias.version) != VersionOrder::Newer)
    return OrdinaryTransactionResult::VersionRejected;
  if (!OrdinaryTransactionDetail::inspect(verify, canonical.stage, Kind::Driver, kCdcCanonicalId, staged) ||
      std::strcmp(staged.version, candidate.version) || std::strcmp(staged.artifact, candidate.artifact))
    return OrdinaryTransactionResult::InvalidStage;
  const auto recovered =
      OrdinaryTransactionDetail::recoverLocked(ops, canonical, Kind::Driver, kCdcCanonicalId, verify, purge, observed);
  if (recovered != OrdinaryTransactionResult::InstalledVerified &&
      recovered != OrdinaryTransactionResult::PreviousRestored &&
      recovered != OrdinaryTransactionResult::NoInstalledPackage)
    return recovered;
  if (ops.exists(canonical.target) && decidePackageVersion(candidate, &observed) != InstallDecision::Upgrade)
    return OrdinaryTransactionResult::VersionRejected;
  CdcMigrationIntent intent{};
  std::strcpy(intent.candidateVersion, candidate.version);
  std::strcpy(intent.aliasVersion, oldAlias.version);
  if (!ops.manifestDigest(canonical.stage, intent.manifestDigest) ||
      !ops.manifestDigest(alias.target, intent.aliasManifestDigest) || !ops.writeIntent(intent))
    return OrdinaryTransactionResult::RestorePending;
  if (!ops.rename(alias.target, kCdcHoldingRoot)) {
    (void)CdcMigrationDetail::recoverLocked(ops, verify, purge, observed);
    return OrdinaryTransactionResult::RenameFailed;
  }
  const auto published = OrdinaryTransactionDetail::publishLocked(ops, candidate, verify, purge, observed, false);
  const auto settled = CdcMigrationDetail::recoverLocked(ops, verify, purge, observed);
  if (settled == OrdinaryTransactionResult::InstalledVerified && !std::strcmp(observed.version, candidate.version) &&
      !ops.exists(canonical.stage))
    return OrdinaryTransactionResult::Published;
  if (published == OrdinaryTransactionResult::Published || published == OrdinaryTransactionResult::CleanupPending)
    return settled == OrdinaryTransactionResult::InstalledVerified ? OrdinaryTransactionResult::Published
                                                                   : OrdinaryTransactionResult::CleanupPending;
  return published;
}
}  // namespace RuntimePackages
