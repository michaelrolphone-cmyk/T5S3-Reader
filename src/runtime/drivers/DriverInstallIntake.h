#pragma once

#include "runtime/packages/PackagePreflight.h"

namespace RuntimeDrivers {

// Decide before creating a download or staging directory. `unresolved` means
// a target/backup/tombstone exists but no installed version could be verified;
// it is NOT the same as a fresh installation. An interrupted stage is retained
// for recovery or inspection, never silently overwritten. The ordinary SHA-256
// digest is only an integrity check, not an authorization grant.
enum class DownloadIntake : unsigned char {
    Fresh, Upgrade, InvalidCandidate, PendingStage,
    UnresolvedGeneration, VersionRejected
};

inline DownloadIntake decideDriverDownload(const char* candidate,
                                           const char* installedVersion,
                                           bool unresolved,
                                           bool pendingStage) {
    if (!RuntimePackages::safeVersion(candidate)) return DownloadIntake::InvalidCandidate;
    if (pendingStage) return DownloadIntake::PendingStage;
    if (unresolved) return DownloadIntake::UnresolvedGeneration;
    if (!installedVersion) return DownloadIntake::Fresh;
    return RuntimePackages::comparePackageVersions(candidate, installedVersion) ==
                   RuntimePackages::VersionOrder::Newer
               ? DownloadIntake::Upgrade : DownloadIntake::VersionRejected;
}

}  // namespace RuntimeDrivers
