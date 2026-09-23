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
// The legacy bool conflates exhaustion with invalid/unverified inventory.
bool nextProvider(const char* capability, uint32_t version, size_t* cursor,
                  char* providerId, size_t capacity);
enum class EnumerationResult : uint8_t { Candidate, Exhausted, Fault };
// A corrupt, inaccessible or unverified inventory is NOT evidence that no
// installed device class exists. Never silently fall through to a competing
// provider after an inventory fault. Call on the same owner task as prepare().
inline EnumerationResult nextProviderChecked(const char* capability,
                                             uint32_t version, size_t* cursor,
                                             char* providerId, size_t capacity) {
    if (providerId && capacity) providerId[0] = 0;
    if (!capability || !*capability || !version || !cursor || !providerId ||
        capacity < 2 || !prepare()) return EnumerationResult::Fault;
    // The verified installer bounds provider IDs to <64 bytes. Callers use a
    // 96-byte identity buffer, so failure after successful prepare() is only
    // normal exhaustion for this checked selector.
    if (capacity < 96) return EnumerationResult::Fault;
    return nextProvider(capability, version, cursor, providerId, capacity)
        ? EnumerationResult::Candidate : EnumerationResult::Exhausted;
}
// Resolves the *named* installed provider, never an ambiguous first match.
bool acquire(const char* providerId, const char* capability, uint32_t version,
             Lease* out);
bool release(Lease* lease);
// Attach only a live exact capability lease to the authenticated app context.
// Registry rights are revoked when this lease or either context terminates.
bool attachStream(const Lease&, uint32_t endpoint, uint32_t rights);
// Retry a failed activation that returned NO grant. The exact provider is
// quiesced before its dependencies are released; no unrelated ELF is stopped.
// An uncertain quiesce leaves the mapping and graph pinned for later retry.
bool recoverFailedProvider(const char* providerId, const char* capability,
                           uint32_t version);
// Refuses destruction while any provider is still granted or not quiescent.
// An unsuccessful shutdown deliberately retains every ELF and package pin.
bool shutdown();
}
