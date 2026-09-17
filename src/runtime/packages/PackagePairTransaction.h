#pragma once

// Transitional transaction for legacy flat /Apps/<name>.elf + <name>.json.
// A new package envelope will use the single-directory package transaction.
// The caller constructs all six trusted, distinct paths from a validated ID.
// Ops exposes exists(), rename(), remove() using HalStorage. Verify MUST check
// matching manifest identity and actual ELF contents; it must not load an ELF.
// This primitive neither authenticates unsigned manifests nor grants rights.
namespace RuntimePackages {

struct PairPaths {
  const char* targetElf;
  const char* targetManifest;
  const char* stageElf;
  const char* stageManifest;
  const char* backupElf;
  const char* backupManifest;
};

inline bool validPairPaths(const PairPaths& p) {
  if (!p.targetElf || !p.targetManifest || !p.stageElf ||
      !p.stageManifest || !p.backupElf || !p.backupManifest) return false;
  const char* names[] = {p.targetElf, p.targetManifest, p.stageElf,
                         p.stageManifest, p.backupElf, p.backupManifest};
  for (unsigned i = 0; i < 6; ++i) {
    if (!names[i][0]) return false;
    for (unsigned j = 0; j < i; ++j) {
      const char* a = names[i];
      const char* b = names[j];
      while (*a && *a == *b) { ++a; ++b; }
      if (*a == *b) return false;
    }
  }
  return true;
}

template <typename Ops>
bool removeIfPresent(Ops& ops, const char* path) {
  return !ops.exists(path) || ops.remove(path);
}

template <typename Ops, typename Verify>
bool recoverPairTransaction(Ops& ops, const PairPaths& p, Verify verify) {
  if (!validPairPaths(p)) return false;
  const bool be = ops.exists(p.backupElf);
  const bool bm = ops.exists(p.backupManifest);
  const bool te = ops.exists(p.targetElf);
  const bool tm = ops.exists(p.targetManifest);

  if (be && bm) {
    // Complete old generation must be proven usable before any target deletion.
    if (!verify(p.backupElf, p.backupManifest)) return false;
    if (te && tm && verify(p.targetElf, p.targetManifest)) {
      // Commit succeeded. Cleanup may be interrupted between the two removes.
      return removeIfPresent(ops, p.backupElf) &&
             removeIfPresent(ops, p.backupManifest);
    }
    // Incomplete publication. These exact managed destination filenames may
    // be discarded only after the backup pair has been verified above.
    if (!removeIfPresent(ops, p.targetElf) ||
        !removeIfPresent(ops, p.targetManifest)) return false;
    if (!ops.rename(p.backupElf, p.targetElf)) return false;
    if (!ops.rename(p.backupManifest, p.targetManifest)) return false;
    return verify(p.targetElf, p.targetManifest);
  }
  if (be) {
    // Cut after first backup rename: old manifest is still in target.
    if (te || !tm || !verify(p.backupElf, p.targetManifest)) return false;
    return ops.rename(p.backupElf, p.targetElf) &&
           verify(p.targetElf, p.targetManifest);
  }
  if (bm) {
    // Either a cut while restoring a verified backup, or interrupted cleanup.
    if (te && tm && verify(p.targetElf, p.targetManifest))
      return removeIfPresent(ops, p.backupManifest);
    if (!te || tm || !verify(p.targetElf, p.backupManifest)) return false;
    return ops.rename(p.backupManifest, p.targetManifest) &&
           verify(p.targetElf, p.targetManifest);
  }
  if (te && !tm) {
    // Interrupted first install: staged JSON may complete the verified pair.
    if (!ops.exists(p.stageManifest) ||
        !verify(p.targetElf, p.stageManifest)) return false;
    return ops.rename(p.stageManifest, p.targetManifest) &&
           verify(p.targetElf, p.targetManifest);
  }
  // Never silently erase an orphan manifest or accept a corrupt full target.
  if (tm && !te) return false;
  return !te || verify(p.targetElf, p.targetManifest);
}

template <typename Ops, typename Verify>
bool publishPairTransaction(Ops& ops, const PairPaths& p, Verify verify,
                            bool replaceAllowed) {
  if (!replaceAllowed || !validPairPaths(p)) return false;
  // Recover FIRST; a failed previous commit must never be overwritten.
  if (!recoverPairTransaction(ops, p, verify)) return false;
  if (!ops.exists(p.stageElf) || !ops.exists(p.stageManifest) ||
      !verify(p.stageElf, p.stageManifest)) return false;
  const bool hadTarget = ops.exists(p.targetElf);
  if (hadTarget && (!ops.exists(p.targetManifest) ||
                    !verify(p.targetElf, p.targetManifest))) return false;
  if (hadTarget) {
    if (!ops.rename(p.targetElf, p.backupElf)) return false;
    if (!ops.rename(p.targetManifest, p.backupManifest)) {
      (void)recoverPairTransaction(ops, p, verify);
      return false;
    }
  }
  if (!ops.rename(p.stageElf, p.targetElf) ||
      !ops.rename(p.stageManifest, p.targetManifest) ||
      !verify(p.targetElf, p.targetManifest)) {
    // Recovery verifies the old pair before removing any partially published
    // executable; if rollback I/O fails, the intact backup is left in place.
    (void)recoverPairTransaction(ops, p, verify);
    return false;
  }
  if (hadTarget) {
    // Cleanup failure leaves a valid target + remaining backup for next boot.
    (void)recoverPairTransaction(ops, p, verify);
  }
  return true;
}
} // namespace RuntimePackages
