#pragma once

#include "PackageOrdinaryInstaller.h"

namespace RuntimePackages {

// Firmware-only, manager-mediated entrypoint. sourceDirectory is an absolute
// HalStorage directory containing .package.json and exactly its declared
// files. No Python, network, ELF execution, driver activation or signing key
// is needed. The caller serializes this operation with all other package
// operations and supplies the REAL runtime capability versions, not a mock.
// Legacy flat /Apps/*.elf and /Drivers/<id>/manifest.json installations are
// unaffected: canonical packages live in the four-kind managed directories.
OrdinaryInstallOutcome installOrdinaryFromSd(
    const char* sourceDirectory, const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*));

// Independently reparse the on-card manifest, verify its complete declared
// inventory and SHA-256, and return its observed identity. Callers must check
// expected kind/ID and current grants before loading or granting privileges.
bool verifyOrdinarySdDirectory(const char* managedDirectory,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), Identity& observed);

} // namespace RuntimePackages
