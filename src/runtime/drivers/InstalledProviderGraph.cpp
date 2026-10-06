#include "runtime/packages/ProviderAbiProfile.h"
#include "runtime/packages/PackageCdcSdMigration.h"
#include "runtime/packages/PackageMutationGate.h"
#include "InstalledProviderGraph.h"
#include "native/NativeStreamBridge.h"
#include "DeviceProviderExecutorV2.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/InstalledProviderRootScan.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinaryStage.h"
#include "runtime/packages/PackageUseGate.h"
#include "runtime/packages/PackageExecutableAdmission.h"
#include "runtime/packages/PackageVerificationReceipt.h"
#include <HalStorage.h>
#include <Arduino.h>
#include <Logging.h>
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
    "xtensa-esp32s3", 2, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
constexpr size_t kMaxProviders = RuntimeProviders::GraphV2::kMaxModules;
// Registration depth is an independent termination/memory bound. Adding room
// for installed modules must not multiply the per-acquisition workspaces.
constexpr size_t kMaxRegistrationDepth = 16;
RuntimeProviders::GraphV2* graph = nullptr;
char pinned[kMaxProviders][96]{};
size_t pinCount = 0;
char loadError[160]{};
struct BootstrapSelection { char id[64]; char capability[64]; uint32_t api; };
BootstrapSelection bootstrap[16]{};
size_t bootstrapCount=0;
bool bootstrapHandoffComplete=true;

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
    uint8_t packageManifestSha256[32]{};
    uint8_t executableDigest[32]{};
    StorageGenerationStamp packageSourceStamp{};
};

struct ProviderAncestry {
    char ids[kMaxRegistrationDepth][64]{};
    // One heap-owned workspace per acquisition, with a distinct slot for
    // each permitted depth. A child cannot overwrite its parent's paths,
    // requirements or import table. Nothing here outlives registration.
    RegistrationFrame frames[kMaxRegistrationDepth]{};
};

// Discovery owns no directory cursor while descending into a dependency.
// Only matching IDs are retained, in enumeration order; admission rereads the
// actual package/profile/imports/ELF and keeps its existing source-stamp checks.
// There is no cross-call cache, including during mutable raw-storage access.
struct ProviderMatches {
    struct Match {
        char id[64]{};
        const Root* root = nullptr;
        Match* next = nullptr;
    };
    static constexpr size_t kLimit =
        (sizeof(kRoots) / sizeof(kRoots[0])) * InstalledProviderRootScan::kOrdinaryLimit;
    Match* first = nullptr;
    Match* last = nullptr;
    size_t count = 0;
    ProviderMatches() = default;
    ProviderMatches(const ProviderMatches&) = delete;
    ProviderMatches& operator=(const ProviderMatches&) = delete;
    ~ProviderMatches() {
        // Iterative destruction: directory-controlled list length is never
        // translated into a recursive destructor chain on loopTask's stack.
        while (first) {
            Match* next = first->next;
            delete first;
            first = next;
        }
    }
    bool append(const Root& root, const char* id) {
        if (count == kLimit) return false;
        Match* item = new (std::nothrow) Match{};
        if (!item) return false;
        std::strcpy(item->id, id); // caller checked the same 64-byte ID bound
        item->root = &root;
        if (last) last->next = item;
        else first = item;
        last = item;
        ++count;
        return true;
    }
};

bool providerFail(const char* stage, const char* identity) {
    std::snprintf(loadError, sizeof(loadError), "%s: %s",
                  stage ? stage : "Provider error",
                  identity ? identity : "unknown");
    return false;
}

void traceStage(const char* capability, const char* stage, uint32_t started, bool okay) {
    // One completion record per existing acquisition stage, never per file,
    // bus transfer or polling tick. Unsigned subtraction permits millis wrap.
    (void)capability; (void)stage; (void)started; (void)okay;
    LOG_INF("PROV", "acquire capability=%s stage=%s elapsed_ms=%lu ok=%d reason=%s",
            capability, stage, static_cast<unsigned long>(millis() - started),
            okay ? 1 : 0, okay ? "none" : lastError());
}

bool pathFor(char (&out)[160], const char* root, const char* id, const char* file) {
    const int length = std::snprintf(out, sizeof(out), "%s/%s/%s", root, id, file);
    return length > 0 && static_cast<size_t>(length) < sizeof(out);
}
uint8_t* readFile(const char* name, size_t maximum, size_t& length) {
    length = 0;
    if (!Storage.ready() || !name) return nullptr;
    HalFile file;
    if (!Storage.openFileForRead("PROV", name, file)) return nullptr;
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


bool profile(const char* bytes,size_t size,char (&capability)[64],uint32_t& api) {
    uint32_t revision=0;
    return parseProviderAbiProfile(bytes,size,revision,capability,api);
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
    if (!verified || !safeId(id) || !expectedCapability || !expectedApi ||
        depth >= kMaxRegistrationDepth) return false;
    // A full graph may still reuse a previously validated provider. Capacity
    // gates only a new registration, never an existing dependency.
    if (destination.hasProvider(id, expectedCapability, expectedApi)) return true;
    if (pinCount >= kMaxProviders || destination.hasProviderId(id)) return false;
    for (size_t i = 0; i < depth; ++i)
        if (std::strcmp(ancestry.ids[i], id) == 0) return false;
    std::snprintf(ancestry.ids[depth], sizeof(ancestry.ids[depth]), "%s", id);
    auto& frame = ancestry.frames[depth];
    auto& target = frame.target;
    const int n = std::snprintf(target, sizeof(target), "%s/%s", root, id);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(target) ||
        !systemPackageUseGate().pin(target)) return false;
    bool accepted = false;
    auto& packageSourceStamp = frame.packageSourceStamp;
    packageSourceStamp = Storage.generation();
    do {
        auto& identity = frame.identity;
        identity = {};
        // Structural verification never trusts requirements to self-authorize.
        // Their actual versions are checked against the verified snapshot
        // below, exactly once per startup rather than by rehashing every ELF.
        if (!inspectInstalledOrdinarySdDirectory(target, kPolicy,
                [](const char*) -> uint32_t { return UINT32_MAX; }, identity) ||
            resourceOnly(identity) || identity.kind != kind || std::strcmp(identity.id, id) ||
            std::strcmp(identity.artifact, "driver.elf")) break;
        auto& name = frame.name;
        if (!pathFor(name, root, id, ".package.json")) break;
        size_t jsonSize = 0;
        uint8_t* json = readFile(name, 4096, jsonSize);
        if (!json) break;
        // A multi-kilobyte manifest plan must not live on loopTask's stack
        // during ELF read, SHA-256 and downstream graph registration.
        std::unique_ptr<OrdinaryPackagePlan> plan(new (std::nothrow) OrdinaryPackagePlan{});
        auto& packageManifestSha256 = frame.packageManifestSha256;
        const bool parsed = plan && packageSnapshotDigest(json,jsonSize,packageManifestSha256) &&
            parseOrdinaryManifest(reinterpret_cast<const char*>(json), jsonSize, *plan);
        std::free(json);
        if (!parsed || resourceOnly(plan->identity) || plan->identity.kind != kind ||
            std::strcmp(plan->identity.id, id) ||
            !preflightCapturedPackage(*plan,kPolicy)) break;
        bool hasProfile = false, hasImports = false, hasExecutable = false;
        auto& executableDigest = frame.executableDigest;
        for (size_t i = 0; i < plan->entryCount; ++i) {
            const auto& entry = plan->entries[i];
            if (!std::strcmp(entry.name, "provider-abi.v1")) hasProfile = true;
            if (!std::strcmp(entry.name, "privileged-imports.v1")) hasImports = true;
            if (!std::strcmp(entry.name, "driver.elf") && entry.executable)
                hasExecutable = receiptDigest(entry.sha256, executableDigest);
        }
        if (!hasProfile || !hasImports || !hasExecutable) break;
        if (!pathFor(name, root, id, "provider-abi.v1")) break;
        size_t profileSize = 0;
        uint8_t* profileBytes = readFile(name, 191, profileSize);
        if (!profileBytes) break;
        auto& capability = frame.capability;
        uint32_t api = 0, osCpuAbi = 0;
        const bool goodProfile = declaredPackageSnapshot(*plan,"provider-abi.v1",profileBytes,profileSize) &&
            parseProviderAbiProfile(reinterpret_cast<const char*>(profileBytes), profileSize, osCpuAbi, capability, api);
        std::free(profileBytes);
        if (!goodProfile || std::strcmp(capability, expectedCapability) ||
            api != expectedApi) break;

        // Match the digest-bound source manifest too. Legacy metadata lacking
        // a source manifest can express only revision 1, never ABI 2.
        bool declaredManifest=false;
        for(size_t i=0;i<plan->entryCount;++i)
            if(!std::strcmp(plan->entries[i].name,"manifest.json"))declaredManifest=true;
        if(declaredManifest) {
            if(!pathFor(name,root,id,"manifest.json"))break;
            size_t manifestSize=0;
            uint8_t* manifest=readFile(name,4096,manifestSize);
            uint32_t declaredRevision=0;
            const bool matching=manifest && declaredPackageSnapshot(*plan,"manifest.json",manifest,manifestSize) &&
                providerManifestOsCpuAbi(reinterpret_cast<const char*>(manifest),manifestSize,declaredRevision) &&
                declaredRevision==osCpuAbi;
            std::free(manifest);
            if(!matching)break;
        } else if(osCpuAbi!=1)break;

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
        const bool goodImports = declaredPackageSnapshot(*plan,"privileged-imports.v1",imports,importsSize) &&
            parseExactImports(imports, importsSize, symbols, symbolCount);
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
        candidate.requiredOsCpuAbi = osCpuAbi;
        candidate.resourceIdentity = plan->identity;
        candidate.declaredSha256 = executableDigest;
        candidate.packageManifestSha256 = packageManifestSha256;
        candidate.packageSourceStamp = packageSourceStamp;
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
    if (!verified || !capability || !minimumApi || depth >= kMaxRegistrationDepth)
        return false;
    auto& frame = ancestry.frames[depth];
    const uint32_t available = versionInInstalledSnapshot(verified, capability);
    if (available < minimumApi)
        return providerFail("Provider dependency unavailable", capability);

    loadError[0] = 0;

    ProviderMatches matches;
    for (const Root& root : kRoots) {
        HalFile directory = Storage.open(root.path, O_RDONLY);
        if (!directory.isOpen() || !directory.isDirectory()) {
            if (directory.isOpen()) (void)directory.close();
            continue;
        }
        InstalledProviderRootScan scan;
        while (true) {
            ordinaryCooperativeYield(1, 1);
            HalFile::DirectoryEntry item;
            if (!directory.readDirectoryEntry(item)) {
                if (directory.getError()) {
                    (void)directory.close();
                    return providerFail("Provider directory read failed", root.path);
                }
                break;
            }
            const auto classification = scan.observe(item.name, item.isDirectory);
            if (classification == InstalledProviderRootScan::Entry::Exhausted) {
                (void)directory.close();
                return providerFail("Provider directory entry limit exceeded", root.path);
            }
            if (classification == InstalledProviderRootScan::Entry::CopyMetadata) continue;
            auto& id = frame.id;
            const size_t length = std::strlen(item.name);
            const bool valid = item.isDirectory && length && length < sizeof(id) &&
                               safeId(item.name);
            if (!valid) continue;
            std::memcpy(id, item.name, length + 1);
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
            if (!matches.append(root, id)) {
                (void)directory.close();
                return providerFail("Provider candidate allocation failed", capability);
            }
        }
        if (!directory.close()) return providerFail("Provider directory close failed", root.path);
    }
    // Complete every checked root scan before entering the recursive loader.
    // At most one root cursor plus the independent package-inspection cursor
    // is live, regardless of dependency depth or the volume's handle count.
    size_t accepted = 0;
    for (const auto* match = matches.first; match && accepted <= 1; match = match->next) {
        if (registerOne(destination, match->root->path, match->id, match->root->kind,
                        capability, available, verified, ancestry, depth))
            ++accepted;
    }
    // Preserve a concrete transitive failure already found while registering
    // this chain, rather than replacing it with the parent's generic name.
    if (!accepted && loadError[0]) return false;
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

bool registerBootstrapPackage(const RuntimePackages::ManagerProviderCandidateV2& candidate, const char* packageRoot) {
    bootstrapHandoffComplete=false;
    if (bootstrapCount==16 || !candidate.driverId || !candidate.provides ||
        std::strlen(candidate.driverId)>=64 || std::strlen(candidate.provides)>=64) return false;
    for(size_t i=0;i<bootstrapCount;++i)
        if(!std::strcmp(bootstrap[i].id,candidate.driverId) ||
           !std::strcmp(bootstrap[i].capability,candidate.provides)) return false;
    if(!graph) {
        graph=new(std::nothrow) RuntimeProviders::GraphV2(nativeProviderStreamHost());
        if(graph) nativeProviderSetOwnerPoll(poll);
    }
    if(!graph || !packageRoot || pinCount==kMaxProviders || std::strlen(packageRoot)>=sizeof(pinned[0]) ||
       !systemPackageUseGate().pin(packageRoot)) return false;
    if(!DeviceProviderExecutorV2::registerManagerValidated(*graph,candidate)) {
        (void)systemPackageUseGate().unpin(packageRoot);
        return false;
    }
    std::strcpy(pinned[pinCount++],packageRoot);
    auto& selected=bootstrap[bootstrapCount++];
    std::strcpy(selected.id,candidate.driverId);std::strcpy(selected.capability,candidate.provides);
    selected.api=candidate.providesApi;
    return true;
}

bool finishBootstrapHandoff() {
    if(!graph || !bootstrapCount) return false;
    bootstrapHandoffComplete=true;
    return true;
}

bool prepare() {
    if(!bootstrapHandoffComplete) return false;
    if (RuntimePackages::cdcMigrationPendingOnSd()) {
        RuntimePackages::ScopedPackageMutation mutation;
        RuntimePackages::Identity recovered{};
        if (mutation) (void)RuntimePackages::reconcileCdcMigrationFromSd(kPolicy,
            [](const char*) -> uint32_t { return UINT32_MAX; }, recovered);
        // Uncertain CDC state blocks only its two roots. Independent software
        // and unrelated hardware providers remain usable during repair.
    }
    if (graph) return true;
    if (!Storage.ready()) return false;
    graph = new (std::nothrow) RuntimeProviders::GraphV2(nativeProviderStreamHost());
    if (graph) nativeProviderSetOwnerPoll(poll);
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
    // Called on the serialized provider owner task. Reuse a complete bounded
    // snapshot only while storage is quiescent in the same observed generation.
    // Input polling starts a new cursor on every tick, not a new installation.
    // Activation still independently validates and pins the selected ELF.
    struct Candidate { char id[64]; char capability[64]; uint32_t api; };
    static Candidate candidates[kMaxProviders]{};
    static size_t count = 0;
    static StorageGenerationStamp snapshotGeneration{};
    static bool retained = false;
    constexpr unsigned kCursorBits = 5;
    constexpr size_t kCursorMask = (size_t{1} << kCursorBits) - 1;
    constexpr size_t kCursorSerialLimit = (SIZE_MAX >> kCursorBits) - 1;
    static_assert(kMaxProviders <= kCursorMask, "Provider cursor position must fit");
    static size_t snapshotSerial = 0;
    static bool cursorExhausted = false;
    if (providerId && capacity) providerId[0] = 0;
    if (!capability || !version || !cursor || !providerId || capacity < 2 ||
        cursorExhausted || !prepare() || !Storage.ready()) {
        if (cursor) *cursor = SIZE_MAX;
        return false;
    }
    // No filesystem or generation calls occur under an additional mutex.
    // Existing HalStorage locks continue to serialize individual media calls.
    const auto observed = Storage.generation();
    size_t position = *cursor & kCursorMask;
    if (*cursor && ((*cursor >> kCursorBits) != snapshotSerial || position > count ||
                    observed.mount != snapshotGeneration.mount ||
                    observed.mutation != snapshotGeneration.mutation)) {
        *cursor = SIZE_MAX;
        return false;
    }
    if (*cursor == 0 && (!retained || !observed.matches(snapshotGeneration))) {
        retained = false;
        count = 0;
        // Invalidate older cursors before even an unsuccessful rebuild. An
        // interleaved cursor must never consume a replacement global snapshot.
        if (snapshotSerial == kCursorSerialLimit) {
            cursorExhausted = true; *cursor = SIZE_MAX; return false;
        }
        ++snapshotSerial;
        std::unique_ptr<RegistrationFrame> frame(new (std::nothrow) RegistrationFrame{});
        if (!frame) { *cursor = SIZE_MAX; return false; }
        for (const Root& root : kRoots) {
            HalFile directory = Storage.open(root.path, O_RDONLY);
            if (!directory.isOpen()) {
                if (Storage.exists(root.path) || !Storage.ready()) {
                    *cursor = SIZE_MAX; count = 0; return false;
                }
                continue;
            }
            if (!directory.isDirectory()) {
                (void)directory.close(); *cursor = SIZE_MAX; count = 0; return false;
            }
            // One extra probe distinguishes the exact bound from truncation.
            bool complete = false;
            InstalledProviderRootScan scan;
            while (true) {
                ordinaryCooperativeYield(1, 1);
                HalFile::DirectoryEntry item;
                if (!directory.readDirectoryEntry(item)) {
                    complete = directory.getError() == 0;
                    break;
                }
                const auto classification = scan.observe(item.name, item.isDirectory);
                if (classification == InstalledProviderRootScan::Entry::Exhausted) break;
                if (classification == InstalledProviderRootScan::Entry::CopyMetadata) continue;
                const size_t length = std::strlen(item.name);
                const bool valid = item.isDirectory && length &&
                    length < sizeof(frame->id) && safeId(item.name);
                if (!valid) continue;
                std::memcpy(frame->id, item.name, length + 1);
                if (RuntimePackages::cdcLineage(root.kind, frame->id) &&
                    RuntimePackages::cdcMigrationPendingOnSd()) continue;
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
            const bool closed = directory.close();
            if (*cursor == SIZE_MAX || !complete || !closed) {
                *cursor = SIZE_MAX; count = 0; return false;
            }
        }
        const auto after = Storage.generation();
        if (observed.mount != after.mount || observed.mutation != after.mutation) {
            *cursor = SIZE_MAX; count = 0; return false;
        }
        snapshotGeneration = after;
        // Active writers/raw access retain operation-local behavior, but may
        // never turn a transient omission into a reusable negative result.
        retained = observed.matches(after);
    }
    while (position < count) {
        const auto& entry = candidates[position++];
        if (entry.api != version || std::strcmp(entry.capability, capability)) continue;
        const size_t length = std::strlen(entry.id);
        if (length >= capacity) { *cursor = SIZE_MAX; return false; }
        std::memcpy(providerId, entry.id, length + 1);
        *cursor = (snapshotSerial << kCursorBits) | position;
        return true;
    }
    *cursor = (snapshotSerial << kCursorBits) | position;
    return false;
}

bool acquire(const char* providerId, const char* capability, uint32_t version,
             Lease* out) {
    if (out) *out = {};
    loadError[0] = 0;
    if (!out || !providerId || !capability || !version)
        return providerFail("Invalid provider acquisition request", providerId);
    if (!prepare()) return providerFail("Provider preparation failed", providerId);
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
bool copyProviderError(const Lease& lease, char* destination, size_t capacity) {
    if (!destination || !capacity) return false;
    destination[0] = 0;
    return graph && lease.interface && graph->interfaceFor(lease.grant) == lease.interface &&
           graph->copyProviderError(lease.grant, destination, capacity);
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
    if (!out || !capability || !minimumVersion)
        return providerFail("Invalid capability acquisition request", capability);
    if (!prepare()) return providerFail("Provider preparation failed", capability);
    // Board profile selections share this graph with ordinary SD providers.
    // Never discover a second owner of an already selected boot capability.
    for(size_t i=0;i<bootstrapCount;++i)
        if(bootstrap[i].api>=minimumVersion && !std::strcmp(bootstrap[i].capability,capability))
            return acquire(bootstrap[i].id,capability,bootstrap[i].api,out);
    uint32_t stageStarted = millis();
    std::unique_ptr<InstalledCapabilitySnapshot, void(*)(InstalledCapabilitySnapshot*)>
        verified(captureInstalledCapabilities(), releaseInstalledCapabilities);
    if (!verified) providerFail("Provider metadata snapshot failed", capability);
    traceStage(capability, "inventory", stageStarted, verified != nullptr);
    if (!verified) return false;
    stageStarted = millis();
    std::unique_ptr<ProviderAncestry> ancestry(new (std::nothrow) ProviderAncestry{});
    uint32_t selected = 0;
    if (!ancestry) providerFail("Provider registration allocation failed", capability);
    const bool registered = ancestry && registerCapability(*graph, verified.get(), capability,
            minimumVersion, &selected, *ancestry, 0);
    traceStage(capability, "registration", stageStarted, registered);
    if (!registered) return false;
    stageStarted = millis();
    const auto grant = graph->acquire(capability, selected);
    const void* interface = graph->interfaceFor(grant);
    traceStage(capability, "activation", stageStarted, interface != nullptr);
    if (!interface) {
        if (grant.slot && !graph->release(grant)) *out = {grant, nullptr};
        return false;
    }
    *out = {grant, interface};
    return true;
}
bool drainExcept(const Lease* retained, size_t count) {
    if (count > RuntimeProviders::GraphV2::kMaxGrants || (count && !retained)) return false;
    if (!graph) return count == 0;
    RuntimeProviders::GrantV2 grants[RuntimeProviders::GraphV2::kMaxGrants]{};
    for (size_t i = 0; i < count; ++i) {
        if (!retained[i].interface || graph->interfaceFor(retained[i].grant) != retained[i].interface) return false;
        grants[i] = retained[i].grant;
    }
    return graph->drainExcept(grants, count);
}
bool shutdown() {
    if (!graph) return true;
    if (!graph->shutdown()) return false;
    delete graph;
    graph = nullptr;
    bootstrapCount=0;
    bootstrapHandoffComplete=true;
    undoPins();
    return true;
}
bool hasLiveGrants() { return graph && graph->liveGrants() != 0; }
} // namespace RuntimeInstalledProviders
