#include "InstalledProviderGraph.h"
#include "DeviceProviderExecutorV2.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageUseGate.h"
#include <HalStorage.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

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

// This reader is deliberately independent of the ELF loader. The package
// adapter separately checks the exact inventory and hashes before this call;
// the executor hashes the private copied relocation bytes a second time.
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
    for (size_t at = 0; at < size;) {
        const size_t n = size - at < 512 ? size - at : 512;
        if (file.read(data + at, n) != static_cast<int>(n)) {
            good = false;
            break;
        }
        at += n;
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

bool registerOne(RuntimeProviders::GraphV2& destination,
                 const char* root, const char* id, Kind kind) {
    if (pinCount >= kMaxProviders || !safeId(id)) return false;
    char target[96]{};
    const int n = std::snprintf(target, sizeof(target), "%s/%s", root, id);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(target) ||
        !systemPackageUseGate().pin(target)) return false;
    // A pinned directory cannot be renamed while its manifest is verified,
    // copied, and registered. Every rejection unpins this exact generation.
    bool accepted = false;
    do {
        Identity identity{};
        if (!verifyOrdinarySdDirectory(target, kPolicy,
                installedCapabilityVersion, identity) || identity.kind != kind ||
            std::strcmp(identity.id, id) ||
            std::strcmp(identity.artifact, "driver.elf")) break;
        char name[160]{};
        if (!pathFor(name, root, id, ".package.json")) break;
        size_t jsonSize = 0;
        uint8_t* json = readFile(name, 4096, jsonSize);
        if (!json) break;
        OrdinaryPackagePlan plan{};
        const bool parsed = parseOrdinaryManifest(
            reinterpret_cast<const char*>(json), jsonSize, plan);
        std::free(json);
        if (!parsed || plan.identity.kind != kind ||
            std::strcmp(plan.identity.id, id)) break;
        bool hasProfile = false, hasImports = false, hasExecutable = false;
        for (size_t i = 0; i < plan.entryCount; ++i) {
            const auto& entry = plan.entries[i];
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
        bool goodImports = true;
        char* begin = reinterpret_cast<char*>(imports);
        char* previous = nullptr;
        for (size_t i = 0; i < importsSize;) {
            char* end = static_cast<char*>(std::memchr(begin, '\n', importsSize - i));
            if (!end || end == begin || static_cast<size_t>(end - begin) >= 128 ||
                symbolCount == 128) { goodImports = false; break; }
            *end = 0;
            if (previous && std::strcmp(previous, begin) >= 0) {
                goodImports = false;
                break;
            }
            symbols[symbolCount++] = begin;
            previous = begin;
            i += static_cast<size_t>(end - begin) + 1u;
            begin = end + 1;
        }
        if (!goodImports || !symbolCount) {
            std::free(imports);
            break;
        }
        if (!pathFor(name, root, id, "driver.elf")) {
            std::free(imports);
            break;
        }
        size_t elfSize = 0;
        uint8_t* elf = readFile(name, 8u * 1024u * 1024u, elfSize);
        if (!elf) { std::free(imports); break; }
        RuntimeProviders::RequirementV2 needs[kMaxPackageRequirements]{};
        bool goodRequirements = plan.requirementCount <= kMaxPackageRequirements;
        for (size_t i = 0; goodRequirements && i < plan.requirementCount; ++i) {
            needs[i] = {plan.requirements[i].capability,
                        installedCapabilityVersion(plan.requirements[i].capability)};
            if (needs[i].api < plan.requirements[i].minApi)
                goodRequirements = false;
        }
        if (goodRequirements) {
            ManagerProviderCandidateV2 candidate{};
            candidate.driverId = id;
            candidate.provides = capability;
            candidate.providesApi = api;
            candidate.requirements = needs;
            candidate.requirementCount = plan.requirementCount;
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
    auto* candidate = new (std::nothrow) RuntimeProviders::GraphV2();
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
            HalFile item = directory.openNextFile();
            if (!item.isOpen()) break;
            char id[64]{};
            const size_t length = item.getName(id, sizeof(id));
            const bool valid = item.isDirectory() && length && length < sizeof(id) &&
                               safeId(id);
            (void)item.close();
            if (valid) (void)registerOne(*candidate, root.path, id, root.kind);
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

bool acquire(const char* providerId, const char* capability, uint32_t version,
             Lease* out) {
    if (out) *out = {};
    if (!out || !providerId || !capability || !version || !prepare()) return false;
    auto grant = graph->acquireFrom(providerId, capability, version);
    const void* interface = graph->interfaceFor(grant);
    if (!interface) {
        if (grant.slot) (void)graph->release(grant);
        return false;
    }
    *out = {grant, interface};
    return true;
}
bool release(Lease* lease) {
    if (!lease || !lease->grant.slot || !graph) return false;
    const bool okay = graph->release(lease->grant);
    if (okay) *lease = {};
    return okay;
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
