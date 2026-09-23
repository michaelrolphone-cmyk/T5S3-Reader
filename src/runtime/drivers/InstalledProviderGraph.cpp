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

namespace RuntimeInstalledProviders {
namespace {
using namespace RuntimePackages;
constexpr PackageRuntimePolicy kPolicy{
    "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
constexpr size_t kMaxProviders = RuntimeProviders::GraphV2::kMaxModules;
RuntimeProviders::GraphV2* graph = nullptr;
char pinned[kMaxProviders][96]{};
size_t pinCount = 0;

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

// The snapshot is owned by prepare() and is never reused after this startup.
// Every directory is still independently checked against its own complete
// inventory and SHA-256 before executable bytes can enter the provider graph.
bool registerOne(RuntimeProviders::GraphV2& destination,
                 const char* root, const char* id, Kind kind,
                 const InstalledCapabilitySnapshot* verified) {
    if (!verified || pinCount >= kMaxProviders || !safeId(id)) return false;
    char target[96]{};
    const int n = std::snprintf(target, sizeof(target), "%s/%s", root, id);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(target) ||
        !systemPackageUseGate().pin(target)) return false;
    bool accepted = false;
    do {
        Identity identity{};
        // Structural verification never trusts requirements to self-authorize.
        // Their actual versions are checked against the verified snapshot
        // below, exactly once per startup rather than by rehashing every ELF.
        if (!verifyOrdinarySdDirectory(target, kPolicy,
                [](const char*) -> uint32_t { return UINT32_MAX; }, identity) ||
            identity.kind != kind || std::strcmp(identity.id, id) ||
            std::strcmp(identity.artifact, "driver.elf")) break;
        char name[160]{};
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
        char capability[64]{};
        uint32_t api = 0;
        const bool goodProfile = profile(reinterpret_cast<const char*>(profileBytes),
                                         profileSize, capability, api);
        std::free(profileBytes);
        if (!goodProfile) break;
        if (!pathFor(name, root, id, "privileged-imports.v1")) break;
        size_t importsSize = 0;
        uint8_t* imports = readFile(name, 128u * 128u, importsSize);
        if (!imports) break;
        const char* symbols[128]{};
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
        RuntimeProviders::RequirementV2 needs[kMaxPackageRequirements]{};
        bool goodRequirements = plan->requirementCount <= kMaxPackageRequirements;
        for (size_t i = 0; goodRequirements && i < plan->requirementCount; ++i) {
            const uint32_t available = versionInInstalledSnapshot(
                verified, plan->requirements[i].capability);
            needs[i] = {plan->requirements[i].capability, available};
            if (available < plan->requirements[i].minApi)
                goodRequirements = false;
        }
        if (goodRequirements) {
            ManagerProviderCandidateV2 candidate{};
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
            accepted = DeviceProviderExecutorV2::registerManagerValidated(destination, candidate);
        }
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
    // One integrity-verified, operation-scoped capability inventory is shared
    // across every registered provider and every one of its requirements.
    // A failed snapshot leaves the existing graph untouched and grants nothing.
    std::unique_ptr<InstalledCapabilitySnapshot, void(*)(InstalledCapabilitySnapshot*)>
        verified(captureInstalledCapabilities(), releaseInstalledCapabilities);
    if (!verified) return false;
    auto* candidate = new (std::nothrow) RuntimeProviders::GraphV2(nativeProviderStreamHost());
    if (!candidate) return false;
    const struct Root { const char* path; Kind kind; } roots[] = {
        {"/Drivers", Kind::Driver}, {"/Providers", Kind::Provider},
        {"/Services", Kind::Service},
    };
    for (const Root& root : roots) {
        HalFile directory = Storage.open(root.path, O_RDONLY);
        if (!directory.isOpen() || !directory.isDirectory()) {
            if (directory.isOpen()) (void)directory.close();
            continue;
        }
        for (size_t i = 0; i < 64 && pinCount < kMaxProviders; ++i) {
            // A bounded scan still needs scheduler cooperation between entries.
            ordinaryCooperativeYield(1, 1);
            HalFile item = directory.openNextFile();
            if (!item.isOpen()) break;
            char id[64]{};
            const size_t length = item.getName(id, sizeof(id));
            const bool valid = item.isDirectory() && length && length < sizeof(id) &&
                               safeId(id);
            (void)item.close();
            if (valid) (void)registerOne(*candidate, root.path, id, root.kind,
                                        verified.get());
        }
        (void)directory.close();
    }
    if (!candidate->moduleCount()) {
        delete candidate;
        undoPins();
        return false;
    }
    graph = candidate;
    return true;
}

bool nextProvider(const char* capability, uint32_t version, size_t* cursor,
                  char* providerId, size_t capacity) {
    if (providerId && capacity) providerId[0] = 0;
    if (!capability || !*capability || !version || !cursor || !providerId ||
        capacity < 2 || !prepare()) return false;
    while (*cursor < graph->moduleCount()) {
        const char* candidate = graph->matchingProviderId((*cursor)++, capability, version);
        if (!candidate) continue;
        const size_t length = std::strlen(candidate);
        if (!length || length >= capacity) return false; // never emit truncated IDs
        std::memcpy(providerId, candidate, length + 1);
        return true;
    }
    return false;
}

bool acquire(const char* providerId, const char* capability, uint32_t version,
             Lease* out) {
    if (out) *out = {};
    if (!out || !providerId || !capability || !version || !prepare()) return false;
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
bool shutdown() {
    if (!graph) return true;
    if (!graph->shutdown()) return false;
    delete graph;
    graph = nullptr;
    undoPins();
    return true;
}
} // namespace RuntimeInstalledProviders
