#pragma once
#include <cstdint>

namespace RuntimePackages {
// Package INSTALL preflight only: resolves capabilities backed by an installed,
// fully integrity-verified canonical provider package and its dependencies.
// This is never an execution grant and never activates an ELF. Runtime grants
// still require provider-graph activation and an execution-context lease.
uint32_t installedCapabilityVersion(const char* capability);

// A startup operation may need many capability queries. Capture the complete,
// independently SHA-256-verified installed inventory once and resolve all of
// that operation's dependencies against the same snapshot. Never retain it
// across installations, SD mutations or separate operations; callers must
// release it before returning. A nullptr is a failed closed snapshot.
struct InstalledCapabilitySnapshot;
InstalledCapabilitySnapshot* captureInstalledCapabilities();
uint32_t versionInInstalledSnapshot(const InstalledCapabilitySnapshot* snapshot,
                                    const char* capability);
void releaseInstalledCapabilities(InstalledCapabilitySnapshot* snapshot);
}
