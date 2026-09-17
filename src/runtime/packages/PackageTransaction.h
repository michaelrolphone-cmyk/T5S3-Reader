#pragma once

namespace RuntimePackages {

// Only runtime-owned, single-directory package layouts may use this primitive.
// All paths are constructed by the caller from a validated package identity;
// this class never interprets untrusted manifest paths or writes through /sd.
// Ops supplies exists(path) and rename(from, to), using HalStorage on device.
// verify(path) must authenticate the identity and validate every managed entry
// (including executable size and digest), without loading any ELF. purge(path)
// must refuse unknown files and must not recursively delete unmanaged content.
struct TransactionPaths {
  const char* target;
  const char* stage;
  const char* backup;
};

template <typename Ops, typename Verify, typename Purge>
bool recoverDirectoryTransaction(Ops& ops, const TransactionPaths& paths,
                                 Verify verify, Purge purge) {
  if (!paths.target || !paths.stage || !paths.backup) return false;
  if (!ops.exists(paths.backup)) {
    return !ops.exists(paths.target) || verify(paths.target);
  }
  // An interrupted target -> backup rename MUST restore the last good copy,
  // never delete it just because another installation is about to begin.
  if (!verify(paths.backup)) return false;
  if (!ops.exists(paths.target)) {
    if (!ops.rename(paths.backup, paths.target)) return false;
    if (verify(paths.target)) return true;
    // Preserve the previous copy if post-rename storage verification fails.
    (void)ops.rename(paths.target, paths.backup);
    return false;
  }
  // Both paths exist after a completed publish. An unknown/corrupt target is
  // never removed automatically, even if there is a valid backup.
  if (!verify(paths.target)) return false;
  return purge(paths.backup);
}

template <typename Ops, typename Verify, typename Purge>
bool publishDirectoryTransaction(Ops& ops, const TransactionPaths& paths,
                                 Verify verify, Purge purge, bool replaceAllowed) {
  // The runtime must refuse replacement while a module is mapped, running,
  // or otherwise pinned. The caller owns the execution-context/lease gate.
  if (!replaceAllowed || !paths.target || !paths.stage || !paths.backup ||
      !ops.exists(paths.stage) || !verify(paths.stage)) return false;
  if (!recoverDirectoryTransaction(ops, paths, verify, purge)) return false;
  const bool hadTarget = ops.exists(paths.target);
  if (hadTarget && !verify(paths.target)) return false;
  if (hadTarget && !ops.rename(paths.target, paths.backup)) return false;
  if (!ops.rename(paths.stage, paths.target)) {
    // If restore fails, the validated old package remains at backup and a
    // later recover() can put it back. Never erase that backup here.
    if (hadTarget) (void)ops.rename(paths.backup, paths.target);
    return false;
  }
  if (!verify(paths.target)) {
    // A failed post-publish check cannot expose an unverified executable.
    // Move it away before attempting restoration; retain backup on failure.
    if (ops.rename(paths.target, paths.stage) && hadTarget)
      (void)ops.rename(paths.backup, paths.target);
    return false;
  }
  // Commit succeeded. Failure to clean the previous generation is harmless:
  // keep the known-good backup for the next verified recovery attempt.
  if (hadTarget) (void)purge(paths.backup);
  return true;
}

}  // namespace RuntimePackages
