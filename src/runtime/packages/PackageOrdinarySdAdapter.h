#pragma once

#include "PackageOrdinaryInstaller.h"

namespace RuntimePackages {

// Firmware-only, manager-mediated entrypoint. sourceDirectory is an absolute
// HalStorage directory containing .package.json and exactly its declared
// files. No Python, network, ELF execution, driver activation or signing key
// is needed. The caller serializes this operation with other package managers
// and supplies actual runtime capability versions, not a mock. Legacy flat
// /Apps/*.elf and /Drivers/<id>/manifest.json installations are preserved.
OrdinaryInstallOutcome installOrdinaryFromSd(
    const char* sourceDirectory, const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*));

// Independently reparse the on-card manifest, verify its complete declared
// inventory and SHA-256, and return observed identity. The caller checks
// kind/ID and execution-context permissions before launching any ELF.
bool verifyOrdinarySdDirectory(const char* managedDirectory,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), Identity& observed);

// Runtime inspection: manifest, inventory, sizes and ELF headers; no hashing.
// Installation/update verification above remains independent and mandatory.
bool inspectInstalledOrdinarySdDirectory(const char* managedDirectory,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), Identity& observed);

// Canonical packages only; legacy files stay under their original managers.
// Uninstall acquires the ordinary package's exclusive mapping lease, commits
// removal by renaming to a tombstone and selectively deletes only declared
// files. Interrupted removals are resumed without resurrecting the ELF.
OrdinaryTransactionResult uninstallOrdinaryFromSd(
    Kind kind, const char* id, const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*));

} // namespace RuntimePackages
