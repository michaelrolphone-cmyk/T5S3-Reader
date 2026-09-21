#pragma once
#include "ProviderGraphV2.h"
#include <cstddef>
#include <cstdint>

// Generic firmware-only bridge. No hardware-specific provider interfaces,
// controller knowledge, or direct peripheral operations belong in this layer.
// All methods execute on the serialized invocation-owner task.
namespace RuntimeInstalledProviders {
struct Lease {
    RuntimeProviders::GrantV2 grant{};
    const void* interface = nullptr;
};

// Import only fully verified canonical package generations into one graph.
// Installs alone do not call this function and do not grant privileges.
bool prepare();
// Enumerate verified candidates for one semantic capability and API version.
// The cursor advances across inspected slots and never activates hardware.
// The identity is copied into caller storage: no graph-owned pointer escapes.
// False means exhausted, unavailable, or invalid; no execution grant results.
bool nextProvider(const char* capability, uint32_t version, size_t* cursor,
                  char* providerId, size_t capacity);
// Resolves the *named* installed provider, never an ambiguous first match.
bool acquire(const char* providerId, const char* capability, uint32_t version,
             Lease* out);
bool release(Lease* lease);
// Retry a failed activation that returned NO grant. The exact provider is
// quiesced before its dependencies are released; no unrelated ELF is stopped.
// An uncertain quiesce leaves the mapping and graph pinned for later retry.
bool recoverFailedProvider(const char* providerId, const char* capability,
                           uint32_t version);
// Refuses destruction while any provider is still granted or not quiescent.
// An unsuccessful shutdown deliberately retains every ELF and package pin.
bool shutdown();
}
