#pragma once
#include <cstdint>

namespace RuntimePackages {
// Package INSTALL preflight only: resolves capabilities backed by an installed,
// structurally inspected canonical provider package and its dependencies.
// This is never an execution grant and never activates an ELF. Runtime grants
// still require provider-graph activation and an execution-context lease.
uint32_t installedCapabilityVersion(const char* capability);

// A startup operation may need many capability queries. Capture the complete,
// structurally inspected installed inventory once and resolve dependencies
// against that same operation-owned snapshot. This legacy metadata query does
// not attest content hashes or coherent generation-bound reuse. It remains
// available to compatible raw-storage applications; runtime activation still
// performs its independent validation. Callers release it before returning.
struct InstalledCapabilitySnapshot;
InstalledCapabilitySnapshot* captureInstalledCapabilities();
uint32_t versionInInstalledSnapshot(const InstalledCapabilitySnapshot* snapshot,
                                    const char* capability);
void releaseInstalledCapabilities(InstalledCapabilitySnapshot* snapshot);
}
