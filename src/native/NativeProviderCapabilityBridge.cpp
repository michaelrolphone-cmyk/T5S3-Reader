#include <T5ProviderCapabilityApi.h>
#include <T5AppApi.h>
#include <NativeAppLauncher.h>
#include "AppManifest.h"
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
#include <Logging.h>

namespace {
using RuntimeInstalledProviders::Lease;
struct Slot {
    Lease provider{};
    uint32_t owner = 0;
    uint32_t generation = 0;
};
Slot active{};
uint32_t generation = 0;

// Only the current invocation may inspect its most recent failed acquire.
// Never return the global log ring to an application. A successful acquire,
// new attempt or app teardown clears the previous error.
char lastFailure[256]{};
uint32_t failureOwner = 0;

uint32_t owner() {
    auto* context = RuntimeResources::ExecutionContext::current();
    return context && context->id() && context->running(context->id()) &&
                   t5_app_get_api(T5_APP_ABI_VERSION) && native_app_current_path()
               ? context->id() : 0;
}
void clearFailure() {
    lastFailure[0] = 0;
    failureOwner = 0;
}
void fail(uint32_t invocation, const char* message) {
    if (!invocation) return;
    failureOwner = invocation;
    (void)std::snprintf(lastFailure, sizeof(lastFailure), "%s", message);
}

// Extract only diagnostics emitted by the privileged-provider diagnostic
// endpoint DURING this acquire. Use the largest overlap between the old ring
// suffix and the new ring prefix to handle the ring wrapping. Never expose
// unrelated firmware logs, old errors, arbitrary provider output or RX data.
bool captureCurrentProviderError(const std::string& before) {
    const std::string after = getLastLogs();
    size_t overlap = before.size() < after.size() ? before.size() : after.size();
    while (overlap && before.compare(before.size() - overlap, overlap,
                                     after, 0, overlap) != 0) --overlap;
    const std::string appended = after.substr(overlap);
    constexpr char marker[] = "[ERR] [PROVELF] ";
    constexpr size_t markerLength = sizeof(marker) - 1;
    std::string extracted;
    size_t start = 0;
    while (start < appended.size()) {
        const size_t found = appended.find(marker, start);
        if (found == std::string::npos) break;
        const size_t content = found + markerLength;
        const size_t end = appended.find('\n', content);
        const size_t stop = end == std::string::npos ? appended.size() : end;
        // Keep the newest two bounded failure lines. The provider logging
        // endpoint admits only USBCTRL/VBUSREF diagnostic prefixes today.
        if (stop > content && stop - content < 192) {
            const std::string line = appended.substr(content, stop - content);
            if (extracted.size() + line.size() + 3 >= sizeof(lastFailure))
                extracted.clear();
            if (!extracted.empty()) extracted += " | ";
            extracted += line;
        }
        start = stop == appended.size() ? stop : stop + 1;
    }
    if (extracted.empty()) return false;
    (void)std::snprintf(lastFailure, sizeof(lastFailure), "%s", extracted.c_str());
    return true;
}

bool lastError(char* output, size_t capacity) {
    const uint32_t invocation = owner();
    if (!invocation || invocation != failureOwner || !lastFailure[0] ||
        !output || !capacity) return false;
    output[0] = 0;
    const int written = std::snprintf(output, capacity, "%s", lastFailure);
    return written >= 0 && static_cast<size_t>(written) < capacity;
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
            if (id[0]) { (void)dir.close(); id[0] = 0; return false; }
            std::strcpy(id, name);
        }
        (void)dir.close();
    }
    return id[0] != 0;
}

bool acquire(const char* capability, uint32_t version,
             t5_provider_capability_lease_t* token, const void** interface) {
    if (token) *token = 0;
    if (interface) *interface = nullptr;
    const uint32_t invocation = owner();
    clearFailure();
    if (!invocation) return false;
    if (!token || !interface || !capability || !version) {
        fail(invocation, "Provider acquire: invalid arguments or API version");
        return false;
    }
    if (active.owner) {
        fail(invocation, "Provider acquire: another capability lease is active");
        return false;
    }
    if (!declaredOptional(capability, version)) {
        fail(invocation, "Provider acquire: app manifest does not authorize this optional capability");
        return false;
    }
    char id[64]{};
    if (!findProvider(capability, version, id)) {
        fail(invocation, "Provider discovery: no unique matching installed profile/API");
        return false;
    }
    const std::string beforeLogs = getLastLogs();
    Lease grant{};
    if (!RuntimeInstalledProviders::acquire(id, capability, version, &grant) ||
        !grant.grant.slot || !grant.interface) {
        fail(invocation, "Provider activation failed (verify, ELF, dependency or hardware start)");
        (void)captureCurrentProviderError(beforeLogs);
        return false;
    }
    generation = generation == UINT32_MAX ? 1u : generation + 1u;
    if (!generation) generation = 1u;
    active.provider = grant;
    active.owner = invocation;
    active.generation = generation;
    *token = generation;
    *interface = grant.interface;
    clearFailure();
    return true;
}

bool release(t5_provider_capability_lease_t token) {
    if (!token || !owner() || active.owner != owner() ||
        token != active.generation) return false;
    Lease grant = active.provider;
    active = {};
    clearFailure();
    // A failed quiesce may consume the grant and quarantine an ELF. Never hand
    // out a stale interface or retry a consumed generation.
    return RuntimeInstalledProviders::release(&grant);
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
    clearFailure();
    if (!active.owner) return;
    Lease grant = active.provider;
    active = {};
    (void)RuntimeInstalledProviders::release(&grant);
}
