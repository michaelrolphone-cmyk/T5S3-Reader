#pragma once
#include <cstdint>

namespace RuntimePackages {
// Package INSTALL preflight only: resolves capabilities backed by an installed,
// fully integrity-verified canonical provider package and its dependencies.
// This is never an execution grant and never activates an ELF. Runtime grants
// still require provider-graph activation and an execution-context lease.
uint32_t installedCapabilityVersion(const char* capability);
}
