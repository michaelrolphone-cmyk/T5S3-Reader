#include <T5ProviderCapabilityApi.h>
#include <T5AppApi.h>
#include <NativeAppLauncher.h>
#include "AppManifest.h"
#include "NativeNavigationInput.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include "runtime/resources/ExecutionContext.h"
#include "runtime/packages/PackageIdentity.h"
#include "runtime/capabilities/AppCapabilityRequirements.h"
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <fcntl.h>

namespace {
using RuntimeInstalledProviders::Lease;
struct Slot {
    Lease provider{};
    uint32_t owner = 0;
    uint32_t generation = 0;
};
constexpr size_t kMaxActiveLeases = 4;
Slot active[kMaxActiveLeases]{};
uint32_t generation = 0;
char error[160]{};
uint32_t errorOwner = 0;
bool fail(const char* message) {
    std::snprintf(error, sizeof(error), "%s", message);
    return false;
}

uint32_t owner() {
    auto* context = RuntimeResources::ExecutionContext::current();
    return context && context->id() && context->running(context->id()) &&
                   t5_app_get_api(T5_APP_ABI_VERSION) && native_app_current_path()
               ? context->id() : 0;
}

// The validated sidecar acts as a bounded development-time allowlist. This is
// not signed app admission, or a replacement for the future trusted picker.
bool declaredOptional(const char* capability, uint32_t version) {
    const char* path = native_app_current_path();
    if (!path || std::strncmp(path, "/sd/", 4) != 0 || !capability || !version ||
        !RuntimeDevices::validCapabilityName(capability)) return false;
    std::string filename(path + 3);
    if (filename.size() < 5 || filename.compare(filename.size() - 4, 4, ".elf")) return false;
    filename.replace(filename.size() - 4, 4, ".json");
    t5_app_manifest_t validated{};
    if (!readAppManifest(filename.c_str(), validated) || !validated.compatible) return false;
    const std::string elf(path + 3);
    if (elf.substr(elf.find_last_of('/') + 1) != validated.file_name) return false;
    const String json = Storage.readFile(filename.c_str());
    if (!json.length() || json.length() > 2048) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc["optional"].is<JsonArray>()) return false;
    for (JsonVariantConst item : doc["optional"].as<JsonArrayConst>()) {
        if (!item.is<JsonObjectConst>() || !item["capability"].is<const char*>() ||
            !item["api"].is<const char*>()) continue;
        const char* name = item["capability"].as<const char*>();
        uint16_t minimum = 0;
        if (std::strcmp(name, capability) == 0 &&
            RuntimeDevices::parseMinimumApi(item["api"].as<const char*>(), &minimum) &&
            version >= minimum) return true;
    }
    return false;
}

// The existing installed graph exposes acquire-from-verified-provider. Select
// an ID using generic, bounded profile metadata (not hardcoded USB names),
// then let the graph re-verify that exact provider and its dependency graph.
// Refuse ambiguous matches rather than choosing an arbitrary implementation.
bool findProvider(const char* capability, uint32_t version, char (&id)[64]) {
    id[0] = 0;
    char expected[120]{};
    const int n = std::snprintf(expected, sizeof(expected),
                                "os-cpu-abi=1\nprovides=%s\napi=%lu\n",
                                capability, static_cast<unsigned long>(version));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(expected)) return false;
    const char* roots[] = {"/Drivers", "/Providers", "/Services"};
    for (const char* root : roots) {
        HalFile dir = Storage.open(root, O_RDONLY);
        if (!dir.isOpen() || !dir.isDirectory()) {
            if (dir.isOpen()) (void)dir.close();
            continue;
        }
        for (unsigned i = 0; i < 64; ++i) {
            HalFile entry = dir.openNextFile();
            if (!entry.isOpen()) break;
            char name[64]{};
            const size_t size = entry.getName(name, sizeof(name));
            const bool directory = entry.isDirectory();
            (void)entry.close();
            if (!directory || !size || size >= sizeof(name) ||
                !RuntimePackages::safeId(name)) continue;
            char file[160]{};
            const int length = std::snprintf(file, sizeof(file),
                                             "%s/%s/provider-abi.v1", root, name);
            if (length <= 0 || static_cast<size_t>(length) >= sizeof(file)) continue;
            HalFile profile = Storage.open(file, O_RDONLY);
            if (!profile.isOpen() || profile.isDirectory()) {
                if (profile.isOpen()) (void)profile.close();
                continue;
            }
            const uint64_t bytes = profile.fileSize64();
            char data[120]{};
            const bool matching = bytes == static_cast<uint64_t>(n) &&
                profile.read(data, static_cast<size_t>(bytes)) == n &&
                std::memcmp(data, expected, static_cast<size_t>(n)) == 0;
            (void)profile.close();
            if (!matching) continue;
            if (id[0]) {
                (void)dir.close(); id[0] = 0;
                return fail("Multiple installed providers match capability");
            }
            std::strcpy(id, name);
        }
        (void)dir.close();
    }
    return id[0] != 0 || fail("No installed provider profile matches capability");
}

bool acquire(const char* capability, uint32_t version,
             t5_provider_capability_lease_t* token, const void** interface) {
    if (token) *token = 0;
    if (interface) *interface = nullptr;
    const uint32_t invocation = owner();
    if (!invocation) return false;
    errorOwner = invocation;
    error[0] = 0;
    if (!token || !interface) return fail("Invalid capability request");
    if (!declaredOptional(capability, version))
        return fail("App JSON missing/invalid optional capability declaration");
    Slot* available = nullptr;
    for (auto& slot : active) {
        if (!slot.owner && !available) available = &slot;
    }
    if (!available) return fail("Capability lease table full");
    char id[64]{};
    if (!findProvider(capability, version, id)) return false;
    Lease grant{};
    if (!RuntimeInstalledProviders::acquire(id, capability, version, &grant) ||
        !grant.grant.slot || !grant.interface) return fail(RuntimeInstalledProviders::lastError());
    generation = generation == UINT32_MAX ? 1u : generation + 1u;
    if (!generation) generation = 1u;
    // The navigation provider interprets which sources overlap this opaque
    // capability. Yield them before the app can subscribe or poll its grant.
    if (!nativeNavigationClaim(generation, capability, version)) {
        (void)RuntimeInstalledProviders::release(&grant);
        return fail("Input ownership handoff failed");
    }
    available->provider = grant;
    available->owner = invocation;
    available->generation = generation;
    *token = generation;
    *interface = grant.interface;
    return true;
}

bool release(t5_provider_capability_lease_t token) {
    const uint32_t invocation = owner();
    if (!token || !invocation) return false;
    for (auto& slot : active) {
        if (slot.owner != invocation || slot.generation != token) continue;
        Lease grant = slot.provider;
        slot = {};
        // A failed quiesce may consume the grant and quarantine an ELF. Never
        // hand out a stale interface or retry a consumed generation.
        const bool released = RuntimeInstalledProviders::release(&grant);
        nativeNavigationRelease(token);
        return released;
    }
    return false;
}
bool lastError(char* out, size_t capacity) {
    if (!out || !capacity) return false;
    out[0] = 0;
    if (!owner() || owner() != errorOwner || !error[0]) return false;
    std::snprintf(out, capacity, "%s", error);
    return true;
}
const t5_provider_capability_api_v1 api = {
    T5_PROVIDER_CAPABILITY_API_VERSION, sizeof(t5_provider_capability_api_v1),
    acquire, release, lastError
};
} // namespace

extern "C" const t5_provider_capability_api_v1*
t5_provider_capability_get_api(uint32_t version) {
    return version == T5_PROVIDER_CAPABILITY_API_VERSION && owner() ? &api : nullptr;
}

// Idempotent loader cleanup before dlclose; no provider retains an app pointer.
extern "C" void native_app_provider_capabilities_release(void) {
    for (auto& slot : active) {
        if (!slot.owner) continue;
        Lease grant = slot.provider;
        const uint32_t token = slot.generation;
        slot = {};
        (void)RuntimeInstalledProviders::release(&grant);
        nativeNavigationRelease(token);
    }
}
