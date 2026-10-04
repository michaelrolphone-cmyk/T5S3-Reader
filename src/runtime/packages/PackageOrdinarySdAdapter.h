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
    uint32_t (*resolveCapability)(const char*), bool allowDowngrade = false);

// Independently reparse the on-card manifest, verify its complete declared
// inventory and SHA-256, and return observed identity. The caller checks
// kind/ID and execution-context permissions before launching any ELF.
bool verifyOrdinarySdDirectory(const char* managedDirectory,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), Identity& observed);

// Runtime inspection: manifest, inventory, sizes and ELF headers; no hashing.
// Installation/update verification above remains independent and mandatory.
// Exact inert host-copy companions are tolerated only here; they cannot stand
// in for declared members. Stage/source verification and purge remain strict.
bool inspectInstalledOrdinarySdDirectory(const char* managedDirectory,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), Identity& observed);
// Returns the exact plan parsed and checked by this inspection. The caller may
// reuse it within the same generation-checked operation, never as authorization.
bool inspectInstalledOrdinarySdDirectory(const char* managedDirectory,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), Identity& observed,
    OrdinaryPackagePlan* inspection);
bool inspectInstalledOrdinarySdDirectory(const char* managedDirectory,
    const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*), Identity& observed,
    OrdinaryPackagePlan* inspection, OrdinaryInspectionDiagnostic* diagnostic);

// Shared legacy-upgrade adapter. Legacy ABI-1 is accepted only at the exact
// managed target/backup; ordinary stage/source verification stays independent.
bool verifyManagedOrdinarySdDirectory(const char* path, Kind kind, const char* id,
    const PackageRuntimePolicy& policy, uint32_t (*resolver)(const char*),
    Identity& observed, bool verifyContents = true);
bool inspectManagedOrdinarySdTree(const char* path, Kind kind, const char* id);
bool purgeManagedOrdinarySdDirectory(const char* path, Kind kind, const char* id);

// Canonical packages only; legacy files stay under their original managers.
// Uninstall acquires the ordinary package's exclusive mapping lease, commits
// removal by renaming to a tombstone and selectively deletes only declared
// files. Interrupted removals are resumed without resurrecting the ELF.
OrdinaryTransactionResult uninstallOrdinaryFromSd(
    Kind kind, const char* id, const PackageRuntimePolicy& policy,
    uint32_t (*resolveCapability)(const char*));

} // namespace RuntimePackages
