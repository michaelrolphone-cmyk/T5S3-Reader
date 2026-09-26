#include "InstalledProviderGraph.h"
#include "native/NativeStreamBridge.h"
#include "DeviceProviderExecutorV2.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinaryStage.h"
#include "runtime/packages/PackageUseGate.h"
#include <HalStorage.h>
#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#if defined(ESP_PLATFORM)
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

// This loader runs below an app on loopTask's 16 KiB stack. Dependency depth
// is bounded, but large frames multiplied by that depth still overflow it.
#if defined(__GNUC__)
#pragma GCC diagnostic error "-Wframe-larger-than=384"
#endif

namespace RuntimeInstalledProviders {
namespace {
using namespace RuntimePackages;
constexpr PackageRuntimePolicy kPolicy{
    "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
constexpr size_t kMaxProviders = RuntimeProviders::GraphV2::kMaxModules;
RuntimeProviders::GraphV2* graph = nullptr;
char pinned[kMaxProviders][96]{};
size_t pinCount = 0;
char loadError[160]{};

struct Root {
    const char* path;
    Kind kind;
};
constexpr Root kRoots[] = {
    {"/Drivers", Kind::Driver},
    {"/Providers", Kind::Provider},
    {"/Services", Kind::Service},
};

struct RegistrationFrame {
    char target[96]{};
    char name[160]{};
    char id[64]{};
    char capability[64]{};
    Identity identity{};
    RuntimeProviders::RequirementV2 needs[kMaxPackageRequirements]{};
    const char* symbols[128]{};
    ManagerProviderCandidateV2 candidate{};
};

struct ProviderAncestry {
    char ids[kMaxProviders][64]{};
    // One heap-owned workspace per acquisition, with a distinct slot for
    // each permitted depth. A child cannot overwrite its parent's paths,
    // requirements or import table. Nothing here outlives registration.
    RegistrationFrame frames[kMaxProviders]{};
};

bool providerFail(const char* stage, const char* identity) {
    std::snprintf(loadError, sizeof(loadError), "%s: %s",
                  stage ? stage : "Provider error",
                  identity ? identity : "unknown");
    return false;
}

bool pathFor(char (&out)[160], const char* root, const char* id, const char* file) {
    const int length = std::snprintf(out, sizeof(out), "%s/%s/%s", root, id, file);
    return length > 0 && static_cast<size_t>(length) < sizeof(out);
}
uint8_t* readFile(const char* name, size_t maximum, size_t& length) {
    length = 0;
    if (!Storage.ready() || !name) return nullptr;
    HalFile file = Storage.open(name, O_RDONLY);
    if (!file.isOpen() || file.isDirectory()) {
        if (file.isOpen()) (void)file.close();
        return nullptr;
    }
    const uint64_t bytes = file.fileSize64();
    if (!bytes || bytes > maximum || bytes > SIZE_MAX - 1u) {
        (void)file.close();
        return nullptr;
    }
    const size_t size = static_cast<size_t>(bytes);
    auto* data = static_cast<uint8_t*>(std::malloc(size + 1));
    if (!data) { (void)file.close(); return nullptr; }
    bool good = true;
#if defined(ESP_PLATFORM)
    size_t lastCheckpoint = 0;
    TickType_t lastTick = xTaskGetTickCount();
#endif
    for (size_t at = 0; at < size;) {
        const size_t n = size - at < 512 ? size - at : 512;
        if (file.read(data + at, n) != static_cast<int>(n)) {
            good = false;
            break;
        }
        at += n;
#if defined(ESP_PLATFORM)
        const TickType_t now = xTaskGetTickCount();
        if (at == size || at - lastCheckpoint >= 4096u ||
            static_cast<TickType_t>(now - lastTick) >= pdMS_TO_TICKS(50)) {
            (void)esp_task_wdt_reset();
            vTaskDelay(1);
            lastCheckpoint = at;
            lastTick = now;
        }
#endif
    }
    if (!file.close()) good = false;
    if (!good) { std::free(data); return nullptr; }
    data[size] = 0;
    length = size;
    return data;
}
bool profile(const char* bytes, size_t size, char (&capability)[64], uint32_t& api) {
    capability[0] = 0;
    api = 0;
    constexpr char prefix[] = "os-cpu-abi=1\nprovides=";
    const size_t prefixSize = sizeof(prefix) - 1;
    if (!bytes || size <= prefixSize + 7 ||
        std::memcmp(bytes, prefix, prefixSize)) return false;
    const char* start = bytes + prefixSize;
    const char* end = std::strchr(start, '\n');
    if (!end || end <= start || static_cast<size_t>(end - start) >= sizeof(capability) ||
        std::strncmp(end, "\napi=", 5)) return false;
    std::memcpy(capability, start, static_cast<size_t>(end - start));
    capability[end - start] = 0;
    const char* number = end + 5;
    if (*number < '1' || *number > '9') return false;
    char* tail = nullptr;
    const unsigned long value = std::strtoul(number, &tail, 10);
    if (!tail || tail == number || *tail != '\n' || tail[1] ||
        !value || value > UINT32_MAX) return false;
    api = static_cast<uint32_t>(value);
    return true;
}

bool parseExactImports(uint8_t* bytes, size_t length,
                       const char* (&symbols)[128], size_t& count) {
    count = 0;
    if (!bytes || !length) return false;
    // A single LF is a canonical, digest-checked declaration of zero imports;
    // the private relocation matcher independently validates the ELF tables.
    if (length == 1 && bytes[0] == '\n') return true;
    char* begin = reinterpret_cast<char*>(bytes);
    char* previous = nullptr;
    for (size_t offset = 0; offset < length;) {
        char* end = static_cast<char*>(std::memchr(begin, '\n', length - offset));
        if (!end || end == begin || static_cast<size_t>(end - begin) >= 128 ||
            count == 128) return false;
        *end = 0;
        for (char* c = begin; c < end; ++c)
            if (static_cast<unsigned char>(*c) <= 0x20 ||
                static_cast<unsigned char>(*c) > 0x7e) return false;
        if (previous && std::strcmp(previous, begin) >= 0) return false;
        symbols[count++] = begin;
        previous = begin;
        offset += static_cast<size_t>(end - begin) + 1u;
        begin = end + 1;
    }
    return count > 0;
}

bool registerCapability(RuntimeProviders::GraphV2& destination,
                        const InstalledCapabilitySnapshot* verified,
                        const char* capability, uint32_t minimumApi,
                        uint32_t* selectedApi, ProviderAncestry& ancestry,
                        size_t depth);

// Metadata is inspected first. ELF/import bytes are read only after the
// requested provider's dependency chain has been resolved successfully.
bool registerOne(RuntimeProviders::GraphV2& destination,
                 const char* root, const char* id, Kind kind,
                 const char* expectedCapability, uint32_t expectedApi,
                 const InstalledCapabilitySnapshot* verified,
                 ProviderAncestry& ancestry, size_t depth) {
    if (!verified || pinCount >= kMaxProviders || !safeId(id) ||
        !expectedCapability || !expectedApi || depth >= kMaxProviders) return false;
    if (destination.hasProvider(id, expectedCapability, expectedApi)) return true;
    if (destination.hasProviderId(id)) return false;
    for (size_t i = 0; i < depth; ++i)
        if (std::strcmp(ancestry.ids[i], id) == 0) return false;
    std::snprintf(ancestry.ids[depth], sizeof(ancestry.ids[depth]), "%s", id);
    auto& frame = ancestry.frames[depth];
    auto& target = frame.target;
    const int n = std::snprintf(target, sizeof(target), "%s/%s", root, id);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(target) ||
        !systemPackageUseGate().pin(target)) return false;
    bool accepted = false;
    do {
        auto& identity = frame.identity;
        identity = {};
        // Structural verification never trusts requirements to self-authorize.
        // Their actual versions are checked against the verified snapshot
        // below, exactly once per startup rather than by rehashing every ELF.
        if (!inspectInstalledOrdinarySdDirectory(target, kPolicy,
                [](const char*) -> uint32_t { return UINT32_MAX; }, identity) ||
            identity.kind != kind || std::strcmp(identity.id, id) ||
            std::strcmp(identity.artifact, "driver.elf")) break;
        auto& name = frame.name;
        if (!pathFor(name, root, id, ".package.json")) break;
        size_t jsonSize = 0;
        uint8_t* json = readFile(name, 4096, jsonSize);
        if (!json) break;
        // A multi-kilobyte manifest plan must not live on loopTask's stack
        // during ELF read, SHA-256 and downstream graph registration.
        std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
        const bool parsed = plan && parseOrdinaryManifest(
            reinterpret_cast<const char*>(json), jsonSize, *plan);
        std::free(json);
        if (!parsed || plan->identity.kind != kind ||
            std::strcmp(plan->identity.id, id)) break;
        bool hasProfile = false, hasImports = false, hasExecutable = false;
        for (size_t i = 0; i < plan->entryCount; ++i) {
            const auto& entry = plan->entries[i];
            if (!std::strcmp(entry.name, "provider-abi.v1")) hasProfile = true;
            if (!std::strcmp(entry.name, "privileged-imports.v1")) hasImports = true;
            if (!std::strcmp(entry.name, "driver.elf") && entry.executable)
                hasExecutable = true;
        }
        if (!hasProfile || !hasImports || !hasExecutable) break;
        if (!pathFor(name, root, id, "provider-abi.v1")) break;
        size_t profileSize = 0;
        uint8_t* profileBytes = readFile(name, 191, profileSize);
        if (!profileBytes) break;
        auto& capability = frame.capability;
        uint32_t api = 0;
        const bool goodProfile = profile(reinterpret_cast<const char*>(profileBytes),
                                         profileSize, capability, api);
        std::free(profileBytes);
        if (!goodProfile || std::strcmp(capability, expectedCapability) ||
            api != expectedApi) break;

        auto& needs = frame.needs;
        bool goodRequirements = plan->requirementCount <= kMaxPackageRequirements;
        for (size_t i = 0; goodRequirements && i < plan->requirementCount; ++i) {
            uint32_t selected = 0;
            if (!registerCapability(destination, verified,
                                    plan->requirements[i].capability,
                                    plan->requirements[i].minApi, &selected,
                                    ancestry, depth + 1)) {
                goodRequirements = false;
                break;
            }
            needs[i] = {plan->requirements[i].capability, selected};
        }
        if (!goodRequirements) break;

        if (!pathFor(name, root, id, "privileged-imports.v1")) break;
        size_t importsSize = 0;
        uint8_t* imports = readFile(name, 128u * 128u, importsSize);
        if (!imports) break;
        auto& symbols = frame.symbols;
        size_t symbolCount = 0;
        const bool goodImports = parseExactImports(imports, importsSize, symbols, symbolCount);
        if (!goodImports) { std::free(imports); break; }
        if (!pathFor(name, root, id, "driver.elf")) {
            std::free(imports);
            break;
        }
        size_t elfSize = 0;
        uint8_t* elf = readFile(name, 8u * 1024u * 1024u, elfSize);
        if (!elf) { std::free(imports); break; }
        auto& candidate = frame.candidate;
        candidate = {};
        candidate.driverId = id;
        candidate.provides = capability;
        candidate.providesApi = api;
        candidate.requirements = needs;
        candidate.requirementCount = plan->requirementCount;
        candidate.elfBytes = elf;
        candidate.elfLength = elfSize;
        candidate.importedSymbols = symbols;
        candidate.importedSymbolCount = symbolCount;
        candidate.requiredOsCpuAbi = 1;
        accepted = DeviceProviderExecutorV2::registerManagerValidated(destination, candidate, false);
        std::free(elf);
        std::free(imports);
    } while (false);
    if (!accepted) {
        (void)systemPackageUseGate().unpin(target);
        return false;
    }
    std::strcpy(pinned[pinCount++], target);
    return true;
}
bool registerCapability(RuntimeProviders::GraphV2& destination,
                        const InstalledCapabilitySnapshot* verified,
                        const char* capability, uint32_t minimumApi,
                        uint32_t* selectedApi, ProviderAncestry& ancestry,
                        size_t depth) {
    if (selectedApi) *selectedApi = 0;
    if (!verified || !capability || !minimumApi || depth >= kMaxProviders)
        return false;
    auto& frame = ancestry.frames[depth];
    const uint32_t available = versionInInstalledSnapshot(verified, capability);
    if (available < minimumApi)
        return providerFail("Provider dependency unavailable", capability);

    size_t accepted = 0;
    for (const Root& root : kRoots) {
        HalFile directory = Storage.open(root.path, O_RDONLY);
        if (!directory.isOpen() || !directory.isDirectory()) {
            if (directory.isOpen()) (void)directory.close();
            continue;
        }
        for (size_t i = 0; i < 64 && accepted <= 1; ++i) {
            ordinaryCooperativeYield(1, 1);
            HalFile item = directory.openNextFile();
            if (!item.isOpen()) break;
            auto& id = frame.id;
            const size_t length = item.getName(id, sizeof(id));
            const bool valid = item.isDirectory() && length && length < sizeof(id) &&
                               safeId(id);
            (void)item.close();
            if (!valid) continue;
            auto& name = frame.name;
            if (!pathFor(name, root.path, id, "provider-abi.v1")) continue;
            size_t profileSize = 0;
            uint8_t* profileBytes = readFile(name, 191, profileSize);
            if (!profileBytes) continue;
            auto& provided = frame.capability;
            uint32_t api = 0;
            const bool matching =
                profile(reinterpret_cast<const char*>(profileBytes), profileSize,
                        provided, api) &&
                std::strcmp(provided, capability) == 0 && api == available;
            std::free(profileBytes);
            if (!matching) continue;
            if (registerOne(destination, root.path, id, root.kind,
                            capability, available, verified, ancestry, depth))
                ++accepted;
        }
        (void)directory.close();
        if (accepted > 1) break;
    }
    if (accepted != 1)
        return providerFail(accepted ? "Provider dependency ambiguous"
                                     : "Provider dependency unavailable",
                            capability);
    if (selectedApi) *selectedApi = available;
    loadError[0] = 0;
    return true;
}

bool registerNamedProvider(RuntimeProviders::GraphV2& destination,
                           const InstalledCapabilitySnapshot* verified,
                           const char* id, const char* capability, uint32_t api,
                           ProviderAncestry& ancestry) {
    const Root* selected = nullptr;
    size_t matches = 0;
    for (const Root& root : kRoots) {
        char target[96]{};
        const int n = std::snprintf(target, sizeof(target), "%s/%s", root.path, id);
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(target)) continue;
        HalFile item = Storage.open(target, O_RDONLY);
        const bool exists = item.isOpen() && item.isDirectory();
        if (item.isOpen()) (void)item.close();
        if (!exists) continue;
        selected = &root;
        ++matches;
    }
    if (matches != 1 || !selected)
        return providerFail(matches ? "Provider package id ambiguous"
                                    : "Provider package not installed",
                            id);
    if (!registerOne(destination, selected->path, id, selected->kind,
                     capability, api, verified, ancestry, 0))
        return providerFail("Provider package failed validation", id);
    loadError[0] = 0;
    return true;
}

void undoPins() {
    while (pinCount) {
        --pinCount;
        (void)systemPackageUseGate().unpin(pinned[pinCount]);
        pinned[pinCount][0] = 0;
    }
}
} // namespace

bool prepare() {
    if (graph) return true;
    if (!Storage.ready()) return false;
    graph = new (std::nothrow) RuntimeProviders::GraphV2(nativeProviderStreamHost());
    return graph != nullptr;
}

const char* lastError() {
    if (loadError[0]) return loadError;
    if (graph && graph->lastError()[0]) return graph->lastError();
    return "Provider inventory verification failed";
}

bool nextProvider(const char* capability, uint32_t version, size_t* cursor,
                  char* providerId, size_t capacity) {
    // Metadata-only enumeration remains separate from lazy ELF admission.
    // Rebuild once at cursor zero; subsequent candidates reuse the same bounded
    // snapshot without repeatedly scanning SD or reading executable payloads.
    struct Candidate { char id[64]; char capability[64]; uint32_t api; };
    static Candidate candidates[kMaxProviders]{};
    static size_t count = 0;
    if (providerId && capacity) providerId[0] = 0;
    if (!capability || !version || !cursor || !providerId || capacity < 2 || !prepare()) {
        if (cursor) *cursor = SIZE_MAX;
        return false;
    }
    if (*cursor == 0) {
        count = 0;
        std::unique_ptr<RegistrationFrame> frame(new (std::nothrow) RegistrationFrame{});
        if (!frame) { *cursor = SIZE_MAX; return false; }
        for (const Root& root : kRoots) {
            HalFile directory = Storage.open(root.path, O_RDONLY);
            if (!directory.isOpen() || !directory.isDirectory()) {
                if (directory.isOpen()) (void)directory.close();
                continue;
            }
            for (size_t visited = 0; visited < 64; ++visited) {
                ordinaryCooperativeYield(1, 1);
                HalFile item = directory.openNextFile();
                if (!item.isOpen()) break;
                const size_t length = item.getName(frame->id, sizeof(frame->id));
                const bool valid = item.isDirectory() && length &&
                    length < sizeof(frame->id) && safeId(frame->id);
                (void)item.close();
                if (!valid) continue;
                if (count == kMaxProviders ||
                    !pathFor(frame->name, root.path, frame->id, "provider-abi.v1")) {
                    *cursor = SIZE_MAX; break;
                }
                size_t size = 0;
                uint8_t* bytes = readFile(frame->name, 191, size);
                uint32_t api = 0;
                const bool parsed = bytes && profile(reinterpret_cast<char*>(bytes),
                    size, frame->capability, api);
                std::free(bytes);
                std::snprintf(frame->target, sizeof(frame->target), "%s/%s", root.path, frame->id);
                if (!parsed || !inspectInstalledOrdinarySdDirectory(frame->target, kPolicy,
                        [](const char*) -> uint32_t { return UINT32_MAX; }, frame->identity) ||
                    frame->identity.kind != root.kind || std::strcmp(frame->identity.id, frame->id)) {
                    *cursor = SIZE_MAX; break;
                }
                auto& entry = candidates[count++];
                std::strcpy(entry.id, frame->id);
                std::strcpy(entry.capability, frame->capability);
                entry.api = api;
            }
            (void)directory.close();
            if (*cursor == SIZE_MAX) { count = 0; return false; }
        }
    }
    while (*cursor < count) {
        const auto& entry = candidates[(*cursor)++];
        if (entry.api != version || std::strcmp(entry.capability, capability)) continue;
        const size_t length = std::strlen(entry.id);
        if (length >= capacity) { *cursor = SIZE_MAX; return false; }
        std::memcpy(providerId, entry.id, length + 1);
        return true;
    }
    return false;
}

bool acquire(const char* providerId, const char* capability, uint32_t version,
             Lease* out) {
    if (out) *out = {};
    loadError[0] = 0;
    if (!out || !providerId || !capability || !version || !prepare()) return false;
    if (!graph->hasProvider(providerId, capability, version)) {
        std::unique_ptr<InstalledCapabilitySnapshot,
                        void(*)(InstalledCapabilitySnapshot*)>
            verified(captureInstalledCapabilities(), releaseInstalledCapabilities);
        std::unique_ptr<ProviderAncestry> ancestry(
            new (std::nothrow) ProviderAncestry{});
        if (!verified || !ancestry)
            return providerFail("Provider metadata snapshot failed", providerId);
        if (!registerNamedProvider(*graph, verified.get(), providerId,
                                   capability, version, *ancestry))
            return false;
    }
    const auto grant = graph->acquireFrom(providerId, capability, version);
    if (!grant.slot) return false;
    const void* interface = graph->interfaceFor(grant);
    if (!interface) {
        // A mapped ELF may be quiescence-uncertain even though its interface
        // cannot be used. Preserve the EXACT grant for checked retry rather
        // than orphaning an occupied slot and losing physical cleanup.
        if (!graph->release(grant)) *out = {grant, nullptr};
        return false;
    }
    *out = {grant, interface};
    return true;
}
void poll() {
    if (!graph) return;
    graph->poll([]() -> uint32_t { return millis(); }, []() {
#if defined(ESP_PLATFORM)
        vTaskDelay(1);
#else
        delay(1);
#endif
    });
}
bool attachStream(const Lease& lease, uint32_t endpoint, uint32_t rights) {
    const uint32_t consumer = nativeProviderStreamConsumer();
    return graph && consumer && lease.interface &&
        graph->interfaceFor(lease.grant) == lease.interface &&
        graph->grantStream(lease.grant, consumer, endpoint, rights);
}
bool release(Lease* lease) {
    if (!lease || !lease->grant.slot || !graph) return false;
    const bool okay = graph->release(lease->grant);
    if (okay) *lease = {};
    return okay;
}
bool recoverFailedProvider(const char* providerId, const char* capability,
                           uint32_t version) {
    // A grantless failed start can retain the mapped provider and its exact
    // dependency interface pointers. Recover only that node; global shutdown
    // would disrupt other services and can discard still-live hardware.
    return graph && providerId && capability && version &&
           graph->recoverFailedFrom(providerId, capability, version);
}
bool acquireCapability(const char* capability, uint32_t minimumVersion, Lease* out) {
    if (out) *out = {};
    loadError[0] = 0;
    if (!out || !capability || !minimumVersion || !prepare()) return false;
    std::unique_ptr<InstalledCapabilitySnapshot, void(*)(InstalledCapabilitySnapshot*)>
        verified(captureInstalledCapabilities(), releaseInstalledCapabilities);
    std::unique_ptr<ProviderAncestry> ancestry(new (std::nothrow) ProviderAncestry{});
    uint32_t selected = 0;
    if (!verified || !ancestry || !registerCapability(*graph, verified.get(), capability,
            minimumVersion, &selected, *ancestry, 0)) return false;
    const auto grant = graph->acquire(capability, selected);
    const void* interface = graph->interfaceFor(grant);
    if (!interface) {
        if (grant.slot && !graph->release(grant)) *out = {grant, nullptr};
        return false;
    }
    *out = {grant, interface};
    return true;
}
bool shutdown() {
    if (!graph) return true;
    if (!graph->shutdown()) return false;
    delete graph;
    graph = nullptr;
    undoPins();
    return true;
}
bool hasLiveGrants() { return graph && graph->liveGrants() != 0; }
} // namespace RuntimeInstalledProviders
