#pragma once
#include "ProviderGraphV2.h"
#include <cstdint>

// Generic firmware-only bridge. No hardware-specific provider interfaces,
// controller knowledge, or direct peripheral operations belong in this layer.
// All methods execute on the serialized invocation-owner task.
namespace RuntimeInstalledProviders {
struct Lease {
    RuntimeProviders::GrantV2 grant{};
    const void* interface = nullptr;
};

// Initialize the installed-provider graph without reading every installed ELF.
// Exact provider/dependency chains are admitted lazily by acquire().
// Installs alone do not call this function and do not grant privileges.
bool prepare();
// Resolves the *named* installed provider, never an ambiguous first match.
bool acquire(const char* providerId, const char* capability, uint32_t version,
             Lease* out);
// Trusted firmware consumers may select one unambiguous installed capability.
// Metadata/dependencies use the same ordinary package admission as named apps.
bool acquireCapability(const char* capability, uint32_t minimumVersion, Lease* out);
bool release(Lease* lease);
bool hasLiveGrants();
const char* lastError();
// Refuses destruction while any provider is still granted or not quiescent.
// An unsuccessful shutdown deliberately retains every ELF and package pin.
bool shutdown();
}
