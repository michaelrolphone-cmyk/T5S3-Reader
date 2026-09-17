#pragma once

#include "ProviderGraphV2.h"
#include <cstddef>
#include <cstdint>

namespace RuntimePackages {

// Input from the unified package manager AFTER ordinary source-independent
// package inspection (bounded paths/identity/version/architecture/dependencies
// and payload integrity). Not an app ABI, signer ticket, or hardware grant.
// The manager MUST hold the package/install-generation lease during this call.
struct ManagerProviderCandidateV2 {
  const char* driverId = nullptr;
  const char* provides = nullptr;
  uint32_t providesApi = 0;
  const RuntimeProviders::RequirementV2* requirements = nullptr;
  size_t requirementCount = 0;
  const uint8_t* elfBytes = nullptr;
  size_t elfLength = 0;
  const char* const* importedSymbols = nullptr;
  size_t importedSymbolCount = 0;
  uint32_t requiredOsCpuAbi = 1;
  // OPTIONAL: checksum declared by this package, for corruption detection.
  // A matching digest is NOT a publisher identity or privilege authorization.
  const uint8_t* declaredSha256 = nullptr;
};

class DeviceProviderExecutorV2 final {
 public:
  // Manager-only native firmware entry. Copies all metadata and executable
  // bytes into the graph; computes SHA-256 itself and optionally compares the
  // package's claimed checksum. Does NOT activate or grant consumer rights.
  // Graph/loader independently enforce generic OS/CPU imports and relocation.
  // No P-256 signer, trust root or NVS security floor is required.
  static bool registerManagerValidated(RuntimeProviders::GraphV2& graph,
                                       const ManagerProviderCandidateV2& input);
};

}  // namespace RuntimePackages
