#pragma once

#include "PackageUseGate.h"

namespace RuntimePackages {

// Only runtime-owned, single-directory layouts may use this primitive. Paths
// are derived from validated identities and writes go through HalStorage.
// Verify checks every managed byte before publication; publisher authentication
// remains a separate prerequisite for the future signed package installer.
struct TransactionPaths {
  const char* target;
  const char* stage;
  const char* backup;
};

namespace detail {
// Caller holds the exclusive package replacement reservation whenever an
// interrupted transaction can rename or remove a published generation.
template <typename Ops, typename Verify, typename Purge>
bool recoverDirectoryTransactionLocked(Ops& ops, const TransactionPaths& paths,
                                       Verify verify, Purge purge) {
  if (!paths.target || !paths.stage || !paths.backup) return false;
  if (!ops.exists(paths.backup)) {
    return !ops.exists(paths.target) || verify(paths.target);
  }
  // A verified published target is the new known-good generation. Cleanup of
  // the old backup may have been interrupted after its first file was deleted.
  if (ops.exists(paths.target)) {
    if (!verify(paths.target)) return false;
    return purge(paths.backup); // Refuse unknown/unmanaged backup entries.
  }
  // A cut after target -> backup requires restoring the complete old package.
  if (!verify(paths.backup)) return false;
  if (!ops.rename(paths.backup, paths.target)) return false;
  if (verify(paths.target)) return true;
  (void)ops.rename(paths.target, paths.backup); // Preserve on bad storage reads.
  return false;
}
}  // namespace detail

template <typename Ops, typename Verify, typename Purge>
bool recoverDirectoryTransaction(Ops& ops, const TransactionPaths& paths,
                                 Verify verify, Purge purge) {
  if (!paths.target || !paths.stage || !paths.backup) return false;
  if (!ops.exists(paths.backup)) {
    // Without a backup this is strictly read-only; do not block inventory
    // queries just because the installed ELF is currently mapped.
    return !ops.exists(paths.target) || verify(paths.target);
  }
  PackageReplacementLease reservation(paths.target);
  if (!reservation) return false;
  return detail::recoverDirectoryTransactionLocked(ops, paths, verify, purge);
}

template <typename Ops, typename Verify, typename Purge>
bool publishDirectoryTransaction(Ops& ops, const TransactionPaths& paths,
                                 Verify verify, Purge purge, bool replaceAllowed) {
  // The caller's permission flag is not enough: the runtime also requires an
  // exclusive reservation across recovery, rename, verification and cleanup.
  if (!replaceAllowed || !paths.target || !paths.stage || !paths.backup) return false;
  PackageReplacementLease reservation(paths.target);
  if (!reservation) return false;
  if (!ops.exists(paths.stage) || !verify(paths.stage)) return false;
  if (!detail::recoverDirectoryTransactionLocked(ops, paths, verify, purge)) return false;
  const bool hadTarget = ops.exists(paths.target);
  if (hadTarget && !verify(paths.target)) return false;
  if (hadTarget && !ops.rename(paths.target, paths.backup)) return false;
  if (!ops.rename(paths.stage, paths.target)) {
    // If restore fails, a later recovery can put the verified backup back.
    if (hadTarget) (void)ops.rename(paths.backup, paths.target);
    return false;
  }
  if (!verify(paths.target)) {
    // A failed post-publish check cannot expose an unverified executable.
    if (ops.rename(paths.target, paths.stage) && hadTarget)
      (void)ops.rename(paths.backup, paths.target);
    return false;
  }
  // The new target is committed. Recovery retries interrupted backup cleanup.
  if (hadTarget) (void)purge(paths.backup);
  return true;
}

}  // namespace RuntimePackages
