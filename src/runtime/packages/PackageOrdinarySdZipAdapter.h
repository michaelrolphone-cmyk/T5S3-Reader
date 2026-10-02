#pragma once

#include "PackageOrdinaryInstaller.h"

namespace RuntimePackages {

// Manager-only, synchronous, offline-capable installation from one immutable
// stored .rte.zip. Path is an absolute HalStorage path (not /sd VFS).
// The manager must serialize mutations per package identity. The archive is
// read by bounded offset operations; publication uses the ordinary transaction
// and never loads or activates the executable.
// expected, when supplied, pins the catalog-selected kind/id/version/artifact.
// No source file or unrelated Inbox entry is removed by this function.
OrdinaryInstallOutcome installOrdinaryFromSdZip(
    const char* archivePath, const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), const Identity* expected = nullptr);

} // namespace RuntimePackages
