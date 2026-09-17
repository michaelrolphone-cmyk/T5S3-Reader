#pragma once

namespace RuntimePackages {

// Only runtime-owned, single-directory package layouts may use this primitive.
// All paths are constructed by the caller from a validated package identity;
// this class never interprets untrusted manifest paths or writes through /sd.
// Ops supplies exists(path) and rename(from, to), using HalStorage on device.
// verify(path) must validate identity and every managed entry (including ELF
// length/digest) without loading it. Authenticity also requires a separate
// signature check when signed packages are introduced. purge(path) must reject
// unknown/unmanaged files; it may clean a partially deleted managed backup.
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
  // A verified published target is the new known-good generation. Cleanup of
  // the old backup may have been interrupted *after* its first file was
  // deleted. Never require the partially cleaned backup to verify before
  // retrying a managed-only purge; doing so would permanently block recovery.
  if (ops.exists(paths.target)) {
    if (!verify(paths.target)) return false; // Preserve unknown/corrupt target.
    return purge(paths.backup);             // Refuse unmanaged backup entries.
  }
  // A cut after target -> backup requires restoring the complete old package.
  // A corrupt backup with no target cannot be recovered automatically.
  if (!verify(paths.backup)) return false;
  if (!ops.rename(paths.backup, paths.target)) return false;
  if (verify(paths.target)) return true;
  // Preserve the previous copy if post-rename storage verification fails.
  (void)ops.rename(paths.target, paths.backup);
  return false;
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
  // The new target is verified and committed. Interrupted backup cleanup may
  // leave a partial managed directory; recovery can retry safely next boot.
  if (hadTarget) (void)purge(paths.backup);
  return true;
}

}  // namespace RuntimePackages
