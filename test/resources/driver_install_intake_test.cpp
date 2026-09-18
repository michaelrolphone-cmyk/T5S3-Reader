#include "runtime/drivers/DriverInstallIntake.h"

#include <cassert>
#include <cstdio>

using RuntimeDrivers::DownloadIntake;
using RuntimeDrivers::decideDriverDownload;

int main() {
    assert(decideDriverDownload("1.0.0", nullptr, false, false) == DownloadIntake::Fresh);
    assert(decideDriverDownload("1.10.0", "1.9.99", false, false) == DownloadIntake::Upgrade);
    assert(decideDriverDownload("1.9.99", "1.10.0", false, false) == DownloadIntake::VersionRejected);
    assert(decideDriverDownload("1.0.0", "1.0.0", false, false) == DownloadIntake::VersionRejected);
    assert(decideDriverDownload("01.0.0", "1.0.0", false, false) == DownloadIntake::VersionRejected);
    assert(decideDriverDownload("1.0", nullptr, false, false) == DownloadIntake::InvalidCandidate);
    assert(decideDriverDownload("1.0.0", nullptr, true, false) == DownloadIntake::UnresolvedGeneration);
    assert(decideDriverDownload("1.0.0", "0.9.0", false, true) == DownloadIntake::PendingStage);
    assert(decideDriverDownload("1.0.0", nullptr, true, true) == DownloadIntake::PendingStage);
    std::puts("Driver download preflight: fresh, numeric upgrade, equal/downgrade, damaged generation and interrupted stage PASS");
}
