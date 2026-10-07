// Production registration with authentic Xtensa packages. Never activates them.
#include <HalStorage.h>
#include <RiscProviderV2.h>
#include <RiscPlatformClockV1.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <freertos/semphr.h>
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include "runtime/drivers/DeviceProviderExecutorV2.h"
#include "runtime/drivers/BootstrapModuleStore.h"
#include "runtime/drivers/InstalledSerialInventory.h"
#include "runtime/drivers/DriverPackage.h"
#include "card_fixture.h"

extern "C" int native_app_register_sd_vfs() { assert(false); return -1; }
const char* native_app_current_path() { return nullptr; }
bool validateDriverPayload(const std::string&, const char*, DriverPackageInfo*, bool) { assert(false); return false; }
bool parseDriverPackageManifest(const std::string&, DriverPackageInfo&) { assert(false); return false; }

// Replace only the standard nothrow allocation entry point. A candidate test
// arms one exact Match-sized allocation; ordinary allocation behavior delegates
// to the normal throwing operator and production code gains no test hook.
static size_t refusedAllocationSize, allocationCountdown;
static bool allocationRefused;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (refusedAllocationSize == size && allocationCountdown && --allocationCountdown == 0) {
        allocationRefused = true;
        return nullptr;
    }
    try { return ::operator new(size); }
    catch (...) { return nullptr; }
}
unsigned long delayCalls;
static std::filesystem::path fixture;
static std::vector<std::string> boundaryPaths;
static std::map<uint32_t, std::string> files, directories;
struct Counts {
    unsigned reads = 0, profiles = 0, roots = 0, next = 0, peak = 0, failures = 0;
    uint64_t bytes = 0;
} counts;
enum class Fault { None, RootRead, RootClose, Generation, ProfileRead, ElfRead };
static Fault fault;
static bool faultFired;
static uint32_t failedDirectory;
static void invalidate() { assert(!FakeLock::held); Storage.invalidateObservations(); }
static bool isRoot(uint32_t handle) {
    const auto found = directories.find(handle);
    return found != directories.end() && found->second == "/Drivers";
}
static uint32_t openFile(void* context, const char* path, uint32_t flags) {
    const auto handle = inspectorExt->file_open(context, path, flags);
    if (handle) assert(files.emplace(handle, path).second);
    return handle;
}
static size_t readFile(void* context, uint32_t handle, void* bytes, size_t length) {
    ++counts.reads;
    const auto& path = files.at(handle);
    if (path.find("/provider-abi.v1") != std::string::npos) ++counts.profiles;
    if (!faultFired && ((fault == Fault::ProfileRead && path == "/Drivers/usb-ui-navigation/provider-abi.v1") ||
        (fault == Fault::ElfRead && path == "/Drivers/gt911-touch/driver.elf"))) {
        faultFired = true;
        return 0;
    }
    const auto received = inspectorApi->file_read(context, handle, bytes, length);
    counts.bytes += received;
    return received;
}
static bool closeFile(void* context, uint32_t handle, bool commit) {
    const bool closed = inspectorApi->file_close(context, handle, commit);
    if (closed) assert(files.erase(handle) == 1);
    return closed;
}
static uint32_t openDirectory(void* context, const char* path) {
    if (!std::strcmp(path, "/Drivers")) ++counts.roots;
    const auto handle = inspectorApi->dir_open(context, path);
    if (handle) {
        assert(directories.emplace(handle, path).second);
        counts.peak = std::max(counts.peak, static_cast<unsigned>(directories.size()));
    } else if (!std::strncmp(path, "/Drivers", 8)) {
        ++counts.failures;
        std::printf("DIR_OPEN_FAILURE path=%s live=%zu\n", path, directories.size());
    }
    return handle;
}
static bool nextDirectory(void* context, uint32_t handle, risc_storage_dirent_v1* entry) {
    ++counts.next;
    if (isRoot(handle) && !faultFired && fault == Fault::RootRead) {
        faultFired = true; failedDirectory = handle; return false;
    }
    const auto okay = inspectorApi->dir_next(context, handle, entry);
    if (okay && isRoot(handle) && !faultFired && fault == Fault::Generation) {
        faultFired = true;
        assert(!FakeLock::afterUnlock);
        FakeLock::afterUnlock = invalidate;
    }
    return okay;
}
static bool closeDirectory(void* context, uint32_t handle) {
    if (isRoot(handle) && !faultFired && fault == Fault::RootClose) {
        faultFired = true; return false;
    }
    const bool closed = inspectorExt->dir_close_checked(context, handle);
    if (closed) {
        assert(directories.erase(handle) == 1);
        if (failedDirectory == handle) failedDirectory = 0;
    }
    return closed;
}
static uint32_t error(void* context, uint32_t handle, bool directory) {
    if (directory && handle == failedDirectory) return 1;
    return inspectorExt->handle_error(context, handle, directory);
}
static void clean() {
    assert(files.empty() && directories.empty() && !FakeLock::held && !FakeLock::afterUnlock);
}
static void writeFile(const std::string& path, const std::string& bytes) {
    auto file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    assert(file && file.write(bytes.data(), bytes.size()) == bytes.size() && file.close());
}
static std::string hostBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return {std::istreambuf_iterator<char>(input), {}};
}
static bool bootRead(const std::string& path, size_t limit, std::vector<uint8_t>& bytes) {
    // Bootstrap root is deliberately short; it maps only to this test fixture.
    assert(path.compare(0, 9, "/fixture/") == 0);
    const auto data = hostBytes(fixture / path.substr(9));
    bytes.assign(data.begin(), data.end());
    return !bytes.empty() && bytes.size() <= limit;
}

// Only this complete production unit takes the ESP scheduler branches. Other
// production units keep their normal host loader/crypto backends. No admission
// function, graph, module, executor, manifest validator, or volume is replaced.
#define ESP_PLATFORM 1
#include "InstalledProviderGraph.inc"
#undef ESP_PLATFORM
using namespace RuntimeInstalledProviders;
using namespace RuntimePackages;

struct Measurement {
    Counts work;
    unsigned long waits;
};
template<class F> static Measurement measured(const char* label, F operation) {
    clean(); counts = {};
    const auto waits = delayCalls;
    const auto sectors = card_reads;
    operation();
    clean();
    std::printf("STAGE %s roots=%u profiles=%u read_calls=%u bytes=%llu upper_waits=%lu "
                "directory_peak=%u directory_failures=%u live=%zu sectors=%u modules=%zu pins=%zu\n",
        label, counts.roots, counts.profiles, counts.reads,
        static_cast<unsigned long long>(counts.bytes), delayCalls - waits, counts.peak,
        counts.failures, directories.size(), card_reads - sectors,
        graph ? graph->moduleCount() : 0, pinCount);
    return {counts, delayCalls - waits};
}
static void boot() {
    assert(loadBootstrapPackages("/fixture", "t5s3-pro", bootRead));
    assert(finishBootstrapHandoff() && graph && graph->moduleCount() == 8 && pinCount == 8);
}
static bool registration(const char* capability, const InstalledCapabilitySnapshot* snapshot) {
    auto ancestry = std::make_unique<ProviderAncestry>();
    uint32_t selected = 0;
    const bool okay = registerCapability(*graph, snapshot, capability, 1, &selected, *ancestry, 0);
    assert(okay ? selected == 1 : selected == 0);
    if (!okay) std::printf("REJECT cap=%s reason=%s\n", capability, lastError());
    return okay;
}
static bool registration(const char* capability) {
    auto* snapshot = captureInstalledCapabilities();
    assert(snapshot);
    const bool okay = registration(capability, snapshot);
    releaseInstalledCapabilities(snapshot);
    return okay;
}
static void prime() {
    auto* snapshot = captureInstalledCapabilities();
    assert(snapshot && versionInInstalledSnapshot(snapshot, "input.navigation") == 1);
    releaseInstalledCapabilities(snapshot);
}
static void cleanup() {
    assert(!hasLiveGrants());
    assert(shutdown() && graph == nullptr && pinCount == 0);
    for (const auto& package : std::filesystem::directory_iterator(fixture / "Drivers")) {
        const auto path = "/Drivers/" + package.path().filename().string();
        assert(!systemPackageUseGate().pinned(path.c_str()));
    }
    for (const auto& path : boundaryPaths) assert(!systemPackageUseGate().pinned(path.c_str()));
    clean();
}
static void navigation(bool original, bool touchFirst) {
    boot();
    RuntimeDevices::Registry registry;
    InstalledSerialInventory serial(registry);
    measured("serial-first", [&] {
        assert(serial.refresh() == (original ? InstalledSerialInventory::Result::Fault : InstalledSerialInventory::Result::Updated));
    });
    const auto serialWarm = measured("serial-ten-repeat", [&] {
        for (unsigned i = 0; i < 10; ++i)
            assert(serial.refresh() == (original ? InstalledSerialInventory::Result::Fault : InstalledSerialInventory::Result::Updated));
    });
    assert(serialWarm.work.reads == 0 && serialWarm.work.roots == 0);
    prime();
    if (touchFirst) measured("touch-first", [&] { assert(registration("input.touch.raw")); });
    const auto first = measured("navigation-first", [&] { assert(registration("input.navigation") != original); });
    if (original) assert(first.work.peak == 8 && first.work.failures == 1 && graph->moduleCount() == (touchFirst ? 9 : 8));
    else assert(first.work.peak == 1 && first.work.failures == 0 && graph->moduleCount() == (touchFirst ? 19 : 18));
    const auto warm = measured("navigation-ten-repeat", [&] {
        for (unsigned i = 0; i < 10; ++i) assert(registration("input.navigation") != original);
    });
    if (original) {
        assert(warm.work.peak == 8 && warm.work.failures == 10);
        assert(warm.work.roots == 100 && warm.work.reads == 2260 && warm.work.profiles == 1970);
        assert(warm.waits == 7430);
    } else {
        assert(warm.work.roots == 10 && warm.work.profiles == 190 && warm.work.reads == 190);
        assert(warm.work.peak == 1 && warm.work.failures == 0);
    }
    measured("touch-after", [&] { assert(registration("input.touch.raw")); });
    assert(graph->moduleCount() == (original ? 9 : 19));
    if (!original) {
        for (const auto& package : std::filesystem::directory_iterator(fixture / "Drivers"))
            assert(graph->hasProviderId(package.path().filename().c_str()));
        Storage.invalidateObservations();
        measured("generation-rebuild", [&] { assert(registration("input.navigation")); });
        const auto rewarm = measured("generation-rebuilt-warm", [&] { assert(registration("input.navigation")); });
        assert(rewarm.work.roots == 1 && rewarm.work.profiles == 19 && rewarm.work.reads == 19);
    }
    cleanup();
}
// Synthetic capacity-boundary identities reuse one unchanged authentic ELF.
// Only these isolated test packages receive generated manifests/profile hashes.
static std::string installBoundaryPackage(unsigned index) {
    const auto id = "capacity-slot-" + std::to_string(index);
    const auto capability = "capacity.slot." + std::to_string(index);
    const auto root = "/Drivers/" + id;
    assert(Storage.mkdir(root.c_str()));
    boundaryPaths.push_back(root);
    const auto manifest = "{\"type\":\"driver\",\"id\":\"" + id +
        "\",\"version\":\"1.0.0\",\"file_name\":\"driver.elf\",\"architecture\":\"xtensa-esp32s3\","
        "\"driver_abi\":2,\"os_cpu_abi\":1,\"provides\":[{\"capability\":\"" + capability +
        "\",\"api\":1}],\"requires\":[]}";
    const std::pair<std::string, std::string> entries[] = {
        {"driver.elf", hostBytes(fixture / "Drivers/t5s3-usb-power-profile/driver.elf")},
        {"provider-abi.v1", "os-cpu-abi=1\nprovides=" + capability + "\napi=1\n"},
        {"manifest.json", manifest}, {"privileged-imports.v1", "\n"}};
    std::string package = "{\"schema\":1,\"kind\":\"driver\",\"id\":\"" + id +
        "\",\"version\":\"1.0.0\",\"artifact\":\"driver.elf\",\"architecture\":\"xtensa-esp32s3\","
        "\"min_runtime_api\":2,\"requires\":[],\"entries\":[";
    for (size_t i = 0; i < 4; ++i) {
        const auto& entry = entries[i];
        uint8_t digest[32]{};
        assert(packageSnapshotDigest(reinterpret_cast<const uint8_t*>(entry.second.data()), entry.second.size(), digest));
        char hash[65]{};
        for (size_t n = 0; n < 32; ++n) std::snprintf(hash + n * 2, 3, "%02x", digest[n]);
        if (i) package += ',';
        package += "{\"name\":\"" + entry.first + "\",\"size_bytes\":" + std::to_string(entry.second.size()) +
            ",\"sha256\":\"" + hash + "\",\"executable\":" + (i == 0 ? "true" : "false") + "}";
        writeFile(root + "/" + entry.first, entry.second);
    }
    writeFile(root + "/.package.json", package + "]}");
    return capability;
}
static void capacity(bool original) {
    boot(); prime();
    const char* order[] = {"board.power.bq25896.profile", "board.power.vbus", "usb.controller", "usb.host",
        "usb.hid", "usb.hid.keyboard", "input.text", "usb.hid.gamepad"};
    measured("topological-eight", [&] { for (const char* capability : order) assert(registration(capability)); });
    assert(graph->moduleCount() == 16 && pinCount == 16);
    measured("topological-rest", [&] {
        for (const char* capability : {"usb.xinput.gamepad", "input.navigation", "input.touch.raw"})
            assert(registration(capability) != original);
    });
    assert(graph->moduleCount() == (original ? 16 : 19));
    measured("full-graph-reuse-and-overflow", [&] {
        if (!original) {
            for (size_t i = graph->moduleCount(); i < RuntimeProviders::GraphV2::kMaxModules; ++i) {
                const auto capability = installBoundaryPackage(static_cast<unsigned>(i));
                assert(registration(capability.c_str()));
            }
            assert(graph->moduleCount() == RuntimeProviders::GraphV2::kMaxModules);
            assert(pinCount == RuntimeProviders::GraphV2::kMaxModules);
            assert(registration("input.navigation"));
            const auto overflow = installBoundaryPackage(static_cast<unsigned>(RuntimeProviders::GraphV2::kMaxModules));
            assert(!registration(overflow.c_str()));
            assert(graph->moduleCount() == RuntimeProviders::GraphV2::kMaxModules);
            assert(pinCount == RuntimeProviders::GraphV2::kMaxModules);
            assert(registration("input.navigation"));
        } else {
            // Original registration rejects even a previously admitted ID at
            // the full pin bound. The candidate must keep safe reuse working.
            assert(!registration("input.text"));
        }
    });
    cleanup();

    // Exercise the graph's exact finite capacity using unmodified authentic
    // ELF bytes under test-only independent IDs. This boundary case does not
    // pretend these duplicated identities were delivered production packages.
    RuntimeProviders::GraphV2 bounded;
    const auto elf = hostBytes(fixture / "Drivers/t5s3-usb-power-profile/driver.elf");
    const char* imports[] = {nullptr};
    ManagerProviderCandidateV2 candidate{};
    candidate.providesApi = 1; candidate.requiredOsCpuAbi = 1;
    candidate.elfBytes = reinterpret_cast<const uint8_t*>(elf.data());
    candidate.elfLength = elf.size(); candidate.importedSymbols = imports;
    for (size_t i = 0; i <= RuntimeProviders::GraphV2::kMaxModules; ++i) {
        const auto id = "capacity-" + std::to_string(i);
        candidate.driverId = id.c_str(); candidate.provides = id.c_str();
        assert(DeviceProviderExecutorV2::registerManagerValidated(bounded, candidate) ==
               (i < RuntimeProviders::GraphV2::kMaxModules));
    }
    assert(bounded.moduleCount() == RuntimeProviders::GraphV2::kMaxModules);
    assert(bounded.liveGrants() == 0 && bounded.shutdown());
    std::puts("Exact production GraphV2 capacity and overflow PASS");
}
static void allocationRefusal() {
#ifdef PROVIDER_HAS_MATCH_LIST
    for (const size_t nth : {size_t{1}, size_t{2}, size_t{4}}) {
        boot(); prime();
        auto* snapshot = captureInstalledCapabilities(); assert(snapshot);
        refusedAllocationSize = sizeof(ProviderMatches::Match);
        allocationCountdown = nth; allocationRefused = false;
        measured("match-allocation-refusal", [&] { assert(!registration("input.navigation", snapshot)); });
        assert(allocationRefused && allocationCountdown == 0);
        assert(std::strstr(lastError(), "Provider candidate allocation failed"));
        assert(graph->moduleCount() == 8 && pinCount == 8);
        refusedAllocationSize = 0;
        measured("match-allocation-retry", [&] { assert(registration("input.navigation", snapshot)); });
        releaseInstalledCapabilities(snapshot);
        assert(graph->moduleCount() == 18 && pinCount == 18);
        cleanup();
    }
#else
    std::puts("Allocation-refusal seam unavailable in this source; no allocation PASS claimed");
#endif
}
static std::string storageBytes(const std::string& path) {
    auto file = Storage.open(path.c_str(), O_RDONLY); assert(file);
    const auto size = file.fileSize64(); assert(size > 0 && size <= 1024 * 1024);
    std::string bytes(static_cast<size_t>(size), '\0');
    assert(file.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size()));
    assert(file.close());
    return bytes;
}
static bool admitTouchSnapshot() {
    const auto metadata = storageBytes("/Drivers/gt911-touch/.package.json");
    auto plan = std::make_unique<OrdinaryPackagePlan>();
    assert(parseOrdinaryManifest(metadata.data(), metadata.size(), *plan));
    const auto trustedMetadata = hostBytes(fixture / "Drivers/gt911-touch/.package.json");
    uint8_t manifestDigest[32]{}, executableDigest[32]{};
    assert(packageSnapshotDigest(reinterpret_cast<const uint8_t*>(trustedMetadata.data()), trustedMetadata.size(), manifestDigest));
    bool found = false;
    for (size_t i = 0; i < plan->entryCount; ++i) if (plan->entries[i].executable) {
        assert(!found && receiptDigest(plan->entries[i].sha256, executableDigest)); found = true;
    }
    assert(found && systemPackageUseGate().pinned("/Drivers/gt911-touch"));
    const auto stamp = Storage.generation();
    const auto image = storageBytes("/Drivers/gt911-touch/driver.elf");
    // Exercise the actual pre-mapping integrity boundary without relocating or
    // executing an Xtensa ELF on this host. Never provide a VerifiedImageCopy.
    return admitInstalledExecutableSnapshot(plan->identity, manifestDigest, executableDigest,
        reinterpret_cast<const uint8_t*>(image.data()), image.size(), stamp);
}
static void installedCorruption() {
    for (const bool profile : {true, false}) {
        boot(); prime();
        auto* snapshot = captureInstalledCapabilities(); assert(snapshot);
        const char* filename = profile ? "provider-abi.v1" : "driver.elf";
        const auto relative = std::string("Drivers/gt911-touch/") + filename;
        const auto original = hostBytes(fixture / relative);
        auto corrupted = original;
        if (profile) {
            const auto at = corrupted.find("os-cpu-abi=1"); assert(at != std::string::npos);
            corrupted[at + std::strlen("os-cpu-abi=")] = '2'; // Still syntactically valid.
        } else {
            corrupted[0] ^= 1; // Invalid ELF magic; original length is preserved.
        }
        writeFile("/" + relative, corrupted);
        measured(profile ? "profile-digest-mismatch" : "elf-header-corruption", [&] {
            assert(!registration("input.touch.raw", snapshot));
        });
        assert(graph->moduleCount() == 8 && pinCount == 8);
        releaseInstalledCapabilities(snapshot);
        writeFile("/" + relative, original);
        measured("restored-package-retry", [&] { assert(registration("input.touch.raw")); });
        assert(graph->moduleCount() == 9 && pinCount == 9);
        cleanup();
    }

    // Executable hashing intentionally occurs at first mapping, after lazy
    // registration has captured bytes. Preserve that distinction explicitly.
    const auto original = hostBytes(fixture / "Drivers/gt911-touch/driver.elf");
    auto corrupted = original; assert(corrupted.size() > 100); corrupted.back() ^= 1;
    writeFile("/Drivers/gt911-touch/driver.elf", corrupted);
    boot(); prime();
    measured("valid-header-corrupt-elf-registered", [&] { assert(registration("input.touch.raw")); });
    measured("corrupt-elf-admission-refused", [&] { assert(!admitTouchSnapshot()); });
    assert(graph->moduleCount() == 9 && pinCount == 9 && graph->liveGrants() == 0);
    cleanup();
    writeFile("/Drivers/gt911-touch/driver.elf", original);
    boot(); prime();
    measured("restored-elf-registration", [&] { assert(registration("input.touch.raw")); });
    measured("restored-elf-admission", [&] { assert(admitTouchSnapshot()); });
    cleanup();
}
static void faults(bool original) {
    if (original) { std::puts("Candidate-only failure/retry checks skipped in original-control mode"); return; }
    for (const auto injected : {Fault::RootRead, Fault::RootClose, Fault::Generation, Fault::ProfileRead, Fault::ElfRead}) {
        boot(); prime();
        auto* snapshot = captureInstalledCapabilities(); assert(snapshot);
        fault = injected; faultFired = false;
        const char* capability = injected == Fault::ElfRead ? "input.touch.raw" : "input.navigation";
        measured(injected == Fault::Generation ? "generation-change-revalidated" : "injected-failure", [&] {
            // A generation-only change may succeed: each selected package is
            // independently revalidated and pinned after discovery completes.
            assert(registration(capability, snapshot) == (injected == Fault::Generation));
        });
        assert(faultFired);
        assert(graph->moduleCount() == (injected == Fault::Generation ? 18 : 8));
        assert(pinCount == graph->moduleCount());
        fault = Fault::None;
        releaseInstalledCapabilities(snapshot);
        // Checked-close uncertainty can retire reusable observations. A fresh
        // generation must still recover; no negative admission result is sticky.
        Storage.invalidateObservations();
        measured("retry-after-fault", [&] { assert(registration(capability)); });
        cleanup();
    }
    allocationRefusal();
    installedCorruption();
}
int main(int argc, char** argv) {
    assert(argc == 4); fixture = argv[1];
    const std::string which = argv[2]; const bool original = std::string(argv[3]) == "original";
    std::setbuf(stdout, nullptr);
    card_image = static_cast<uint8_t*>(std::calloc(card_sectors, 512)); assert(card_image); format(false);
    const auto* driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    inspectorApi = static_cast<const risc_storage_volume_api_v1*>(driver->capability);
    inspectorExt = risc_storage_volume_extension(inspectorApi);
    risc_platform_clock_api_v1 clock = {1, sizeof(clock), nullptr, now, sleep};
    risc_provider_dependency_v1 dependencies[] = {{"platform.clock", 1, &clock}};
    assert(driver->start(dependencies, 1));
    auto hooks = *inspectorExt; hooks.base.struct_size = sizeof(hooks);
    hooks.file_open = openFile; hooks.base.file_read = readFile; hooks.base.file_close = closeFile;
    hooks.base.dir_open = openDirectory; hooks.base.dir_next = nextDirectory;
    hooks.dir_close_checked = closeDirectory; hooks.handle_error = error;
    assert(Storage.bindVolume(&hooks.base) && Storage.mkdir("/Apps") && Storage.mkdir("/Drivers"));
    // Sorted installation gives reproducible FAT layout. Logical-call budgets
    // are asserted; sector totals remain informational transport-model evidence.
    std::map<std::string, std::filesystem::path> packages;
    for (const auto& entry : std::filesystem::directory_iterator(fixture / "Drivers"))
        packages.emplace(entry.path().filename().string(), entry.path());
    for (const auto& package : packages) {
        const auto target = "/Drivers/" + package.first; assert(Storage.mkdir(target.c_str()));
        std::map<std::string, std::filesystem::path> entries;
        for (const auto& entry : std::filesystem::directory_iterator(package.second))
            entries.emplace(entry.path().filename().string(), entry.path());
        for (const auto& entry : entries) writeFile(target + "/" + entry.first, hostBytes(entry.second));
    }
    if (which == "navigation" || which == "touch-first") navigation(original, which == "touch-first");
    else if (which == "capacity") capacity(original);
    else if (which == "faults") faults(original);
    else assert(false);
    clean(); assert(driver->quiesce()); driver->stop(); std::free(card_image);
    std::printf("Production provider admission %s (%s) PASS\n", which.c_str(), original ? "original control" : "candidate");
}
