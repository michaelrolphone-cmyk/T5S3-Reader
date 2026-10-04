#include "app_inventory_card.inc"
#include <AppManifestRules.h>
#include <Logging.h>
#include <esp_task_wdt.h>
#include "native/AppManifest.h"
#include "native/AppPackageInstaller.h"
#include "native/InstalledAppPath.h"
extern "C" {
#include "ff.h"
}

static const char* activePath;
const char* native_app_current_path() { return activePath; }
struct AppSession { std::vector<t5_app_manifest_t> installed; } session;
static AppSession* current() { return &session; }
#include "app_inventory_host.inc"

static unsigned opens, stats, nexts, closes, backupStats, liveHandles;
static bool syntheticCandidates;
#ifndef APP_INVENTORY_BASELINE
enum class InjectionPoint { None, Scan, End, Probe };
enum class InjectionAction { Invalidate, Backup, Writer, Remount, External, Uncertain };
static InjectionPoint injectionPoint;
static InjectionAction injectionAction;
static bool injected;
static HalFile concurrentWriter;
static void inject();
static void queueInjection(InjectionPoint point) {
    if (injectionPoint != point || injected) return;
    injected = true;
    FakeLock::afterUnlock = inject;
}
#endif
static uint32_t rootHandle;
static bool failRootRead;
static const char* repeatedRootName;
static unsigned closeFailureCountdown;
static size_t rootEntries, failAtEntry;
static uint32_t countedOpen(void* context, const char* path, uint32_t flags) {
    ++opens;
    const auto h = inspectorExt->file_open(context, path, flags);
    if (h) ++liveHandles;
    return h;
}
static bool countedStat(void* context, const char* path, uint64_t* size, bool* dir) {
    ++stats;
    const std::string name(path);
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".bak") ++backupStats;
    const bool found = inspectorApi->stat(context, path, size, dir);
#ifndef APP_INVENTORY_BASELINE
    if (name == "/Apps/example_app_0.elf") queueInjection(InjectionPoint::Probe);
#endif
    return found;
}
static uint32_t countedDirOpen(void* context, const char* path) {
    ++opens;
    const auto h = inspectorApi->dir_open(context, path);
    if (h) ++liveHandles;
    if (!std::strcmp(path, "/Apps")) { rootHandle = h; rootEntries = 0; }
    return h;
}
static bool countedNext(void* context, uint32_t h, risc_storage_dirent_v1* entry) {
    ++nexts;
    if (h == rootHandle && failRootRead && rootEntries == failAtEntry) return false;
    if (h == rootHandle) {
        ++rootEntries;
#ifndef APP_INVENTORY_BASELINE
        if (rootEntries == 4) queueInjection(InjectionPoint::Scan);
#endif
        if (syntheticCandidates) {
            *entry = {}; std::snprintf(entry->name, sizeof(entry->name), "staged_%zu.elf.part", rootEntries);
            return true;
        }
        if (repeatedRootName) { *entry = {}; std::strcpy(entry->name, repeatedRootName); return true; }
    }
    return inspectorApi->dir_next(context, h, entry);
}
static bool countedDirClose(void* context, uint32_t h) {
    ++closes;
    if (h == rootHandle && closeFailureCountdown && --closeFailureCountdown == 0) return false;
    const bool closed = inspectorExt->dir_close_checked(context, h);
    if (closed) {
        assert(liveHandles); --liveHandles;
#ifndef APP_INVENTORY_BASELINE
        if (h == rootHandle) queueInjection(InjectionPoint::End);
#endif
    }
    return closed;
}
static bool countedFileClose(void* context, uint32_t h, bool commit) {
    ++closes;
    const bool closed = inspectorApi->file_close(context, h, commit);
    if (closed) { assert(liveHandles); --liveHandles; }
    return closed;
}
static uint32_t countedError(void* context, uint32_t h, bool dir) {
    if (dir && h == rootHandle && failRootRead && rootEntries == failAtEntry) return 1;
    return inspectorExt->handle_error(context, h, dir);
}
static void write(const std::string& path, const std::string& bytes) {
    auto file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    assert(file && file.write(bytes.data(), bytes.size()) == bytes.size() && file.close());
}
#ifndef APP_INVENTORY_BASELINE
static void inject() {
    assert(!FakeLock::held);
    switch (injectionAction) {
    case InjectionAction::Invalidate: Storage.invalidateObservations(); break;
    case InjectionAction::Backup: write("/Apps/example_app_0.elf.BAK", "concurrent backup"); break;
    case InjectionAction::Writer:
        concurrentWriter = Storage.open("/Apps/inventory-writer", O_WRONLY | O_CREAT);
        assert(concurrentWriter); break;
    case InjectionAction::Remount:
        // A live cursor blocks remount, but even that rejected attempt retires
        // the stamp. At scan end no handles remain and remount must succeed.
        assert(Storage.begin() == (liveHandles == 0)); break;
    case InjectionAction::External: Storage.externalStorageBegin(); break;
    case InjectionAction::Uncertain: Storage.externalStorageUncertain(); break;
    }
}
static void arm(InjectionPoint point, InjectionAction action) {
    assert(!FakeLock::afterUnlock);
    injectionPoint = point; injectionAction = action; injected = false;
}
static void disarm() {
    assert(injected && !FakeLock::afterUnlock); injectionPoint = InjectionPoint::None;
}
#endif
static std::vector<std::string> manifests() {
    std::vector<std::string> result;
    for (const auto& app : session.installed) {
        // Every public field, in published order; do not compare padding.
        result.push_back(std::string(app.display_name) + "|" + app.file_name + "|" +
            app.min_firmware_version + "|" + app.icon + "|" + (app.compatible ? "1" : "0"));
    }
    return result;
}
static void assertClean() {
    assert(liveHandles == 0 && !FakeLock::held && !FakeLock::afterUnlock);
}
static std::string appJson(const char* artifact, const char* label = "App") {
    return std::string("{\"display_name\":\"") + label + "\",\"file_name\":\"" + artifact +
        "\",\"version\":\"1.0.0\",\"min_firmware_version\":\"1.0.0\",\"icon\":\"solid:f013\"}";
}
static void pair(const std::string& root, const char* artifact, const char* label = "App") {
    write(root + "/" + artifact, elfBytes());
    const std::string filename(artifact);
    write(root + "/" + filename.substr(0, filename.size() - 4) + ".json", appJson(artifact, label));
}
static void managed(const char* id, const char* artifact) {
    const std::string root = std::string("/Apps/") + id;
    assert(Storage.mkdir(root.c_str())); pair(root, artifact, id);
    const std::string filename(artifact), sidecar = filename.substr(0, filename.size() - 4) + ".json";
    std::string json = "{\"schema\":1,\"kind\":\"application\",\"id\":\"" + std::string(id) +
        "\",\"version\":\"1.0.0\",\"artifact\":\"" + artifact +
        "\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":2,\"entries\":[";
    const std::pair<std::string, std::string> files[] = {{artifact, elfBytes()}, {sidecar, appJson(artifact, id)}};
    for (unsigned i = 0; i < 2; ++i) {
        if (i) json += ',';
        json += "{\"name\":\"" + files[i].first + "\",\"size_bytes\":" + std::to_string(files[i].second.size()) +
            ",\"sha256\":\"" + std::string(64, 'a') + "\",\"executable\":" + (i ? "false" : "true") + "}";
    }
    write(root + "/.package.json", json + "],\"requires\":[]}");
}
struct Measurements {
    unsigned read = card_reads, open = opens, stat = stats, next = nexts, close = closes;
    uint64_t began = card_time;
    void report(const char* name) const {
        std::printf("%s sectors=%u opens=%u stats=%u next=%u closes=%u modeled_ms=%llu\n", name,
            card_reads-read, opens-open, stats-stat, nexts-next, closes-close, (unsigned long long)(card_time-began));
    }
};
int main(int argc, char** argv) {
    std::setbuf(stdout, nullptr);
    card_image = static_cast<uint8_t*>(std::calloc(card_sectors, 512)); assert(card_image); format(false);
    const auto* driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    inspectorApi = static_cast<const risc_storage_volume_api_v1*>(driver->capability);
    inspectorExt = risc_storage_volume_extension(inspectorApi);
    risc_platform_clock_api_v1 clock = {1, sizeof(clock), nullptr, now, sleep};
    risc_provider_dependency_v1 dependencies[] = {{"platform.clock", 1, &clock}};
    assert(driver->start(dependencies, 1));
    auto hooks = *inspectorExt; hooks.base.struct_size = sizeof(hooks);
    hooks.file_open = countedOpen; hooks.base.stat = countedStat; hooks.base.dir_open = countedDirOpen;
    hooks.base.dir_next = countedNext; hooks.dir_close_checked = countedDirClose;
    hooks.base.file_close = countedFileClose; hooks.handle_error = countedError;
    assert(Storage.bindVolume(&hooks.base) && Storage.mkdir("/Apps"));
    for (unsigned i = 0; i < 37; ++i) {
        const auto name = "example_app_" + std::to_string(i) + ".elf";
        char label[16]; std::snprintf(label, sizeof(label), "App %02u", i);
        pair("/Apps", name.c_str(), label);
    }
    pair("/Apps", "springboard.elf");
    for (unsigned i = 0; i < 8; ++i) write("/Apps/notes_" + std::to_string(i), "untouched");
#ifndef APP_INVENTORY_BASELINE
    if (argc > 1 && !std::strncmp(argv[1], "uncertain-", 10)) {
        const auto point = !std::strcmp(argv[1], "uncertain-scan") ? InjectionPoint::Scan :
            !std::strcmp(argv[1], "uncertain-end") ? InjectionPoint::End : InjectionPoint::Probe;
        arm(point, InjectionAction::Uncertain);
        const auto before = backupStats;
        assert(installedRefresh() && session.installed.size() == 37);
        disarm(); assert(backupStats - before == 74 && !Storage.generation().quiescent);
        assertClean();
        assert(!Storage.begin());
        assert(installedRefresh() && session.installed.size() == 37);
        assertClean();
        std::printf("%s: fallback persists after uncertain access PASS\n", argv[1]);
        assert(driver->quiesce()); driver->stop(); std::free(card_image);
        return 0;
    }
#endif
    if (argc > 1) {
        std::string path;
        closeFailureCountdown = !std::strcmp(argv[1], "close-inventory") ? 2 : 1;
        if (!std::strcmp(argv[1], "close-resolve")) assert(!resolveInstalledAppPath("example_app_0.elf", path) && path.empty());
        else if (!std::strcmp(argv[1], "close-recovery")) assert(!RuntimePackages::recoverAppInventory());
        else assert(!installedRefresh() && session.installed.empty());
        assert(!closeFailureCountdown && !Storage.generation().quiescent);
        assertClean();
#ifndef APP_INVENTORY_BASELINE
        StorageGenerationStamp rejected = Storage.generation();
        assert(RuntimePackages::recoverAppInventory(&rejected) && !rejected.quiescent);
        const auto before = backupStats;
        assert(installedRefresh() && session.installed.size() == 37 && backupStats - before == 74);
        assertClean();
#endif
        std::printf("%s: rejected incomplete close and safe retry PASS\n", argv[1]);
        assert(driver->quiesce()); driver->stop(); std::free(card_image);
        return 0;
    }
    inventory_sector_ms = 3;
    {
        Measurements m; assert(RuntimePackages::recoverAppInventory()); m.report("recovery-84-entries");
#ifndef APP_INVENTORY_BASELINE
        assert(opens - m.open == 1 && closes - m.close == 1 && card_reads - m.read < 64);
#endif
    }
    {
        Measurements m; std::string path;
        assert(resolveInstalledAppPath("example_app_0.elf", path) && path == "/sd/Apps/example_app_0.elf");
        assert(resolveInstalledAppPath("example_app_36.elf", path));
        m.report("home-two-pins");
#ifndef APP_INVENTORY_BASELINE
        assert(opens - m.open <= 14 && card_reads - m.read < 1000);
#endif
    }
    {
        Measurements m; std::string path;
        for (const char* name : {"example_app_0.elf", "example_app_12.elf", "example_app_24.elf", "example_app_36.elf"})
            assert(resolveInstalledAppPath(name, path));
        m.report("home-four-pins");
#ifndef APP_INVENTORY_BASELINE
        assert(opens - m.open <= 28 && card_reads - m.read < 2000);
#endif
    }
    {
        Measurements m; const auto before = backupStats;
        assert(installedRefresh() && session.installed.size() == 37); m.report("springboard-37-apps");
#if !defined(APP_INVENTORY_BASELINE) || defined(APP_INVENTORY_REQUIRE_PROBE_BUDGET)
        assert(stats - m.stat == 39 && backupStats == before && card_reads - m.read < 2500);
#else
        (void)before;
#endif
        for (const auto& manifest : manifests()) std::printf("MANIFEST %s\n", manifest.c_str());
        assertClean();
#ifndef APP_INVENTORY_BASELINE
        assert(opens - m.open < 50 && card_reads - m.read < 4500);
#endif
    }
#ifdef APP_INVENTORY_BASELINE
    assert(driver->quiesce()); driver->stop(); std::free(card_image);
    return 0;
#endif
    inventory_sector_ms = 0;
#ifndef APP_INVENTORY_BASELINE
    const auto expected = manifests();
    StorageGenerationStamp noBackups{};
    assert(RuntimePackages::recoverAppInventory(&noBackups) && Storage.unchanged(noBackups));
    assertClean();
    assert(Storage.begin() && !Storage.unchanged(noBackups));
    assert(RuntimePackages::recoverAppInventory(&noBackups) && Storage.unchanged(noBackups));

    // Conservative fallbacks publish the identical complete manifest sequence.
    // Mixed-case suffixes and directories are visible to FAT pathname stat.
    for (const char* suffix : {".bak", ".BAK", ".BaK"}) {
        const auto backup = std::string("/Apps/unrelated notes") + suffix;
        for (bool directory : {false, true}) {
            if (directory) assert(Storage.mkdir(backup.c_str())); else write(backup, "preserve");
            noBackups = Storage.generation();
            assert(RuntimePackages::recoverAppInventory(&noBackups) && !noBackups.quiescent);
            const auto before = backupStats;
            assert(installedRefresh() && manifests() == expected && backupStats - before == 74);
            assert(directory ? Storage.rmdir(backup.c_str()) : Storage.remove(backup.c_str()));
            assertClean();
        }
    }
    for (const char* suffix : {".elf.bak", ".json.bak", ".elf.BAK", ".json.BaK"}) {
        const auto backup = std::string("/Apps/example_app_0") + suffix;
        for (bool directory : {false, true}) {
            if (directory) assert(Storage.mkdir(backup.c_str())); else write(backup, "preserve");
            activePath = "/sd/Apps/example_app_0.elf";
            noBackups = Storage.generation();
            (void)RuntimePackages::recoverAppInventory(&noBackups);
            assert(!noBackups.quiescent);
            const auto before = backupStats;
            assert(installedRefresh() && session.installed.size() == 36 && backupStats > before);
            for (const auto& app : session.installed) assert(std::strcmp(app.file_name, "example_app_0.elf"));
            assert(Storage.exists(backup.c_str()));
            assert(directory ? Storage.rmdir(backup.c_str()) : Storage.remove(backup.c_str()));
            activePath = nullptr; assertClean();
        }
    }
    // Ask real FatFs for the generated short alias, rather than guessing it.
    write("/Apps/unrelated long backup.bak", "preserve");
    FILINFO alias{};
    assert(f_stat("/Apps/unrelated long backup.bak", &alias) == FR_OK && alias.altname[0]);
    assert(Storage.exists((std::string("/Apps/") + alias.altname).c_str()));
    noBackups = Storage.generation();
    assert(RuntimePackages::recoverAppInventory(&noBackups) && !noBackups.quiescent);
    assert(installedRefresh() && manifests() == expected);
    assert(Storage.remove("/Apps/unrelated long backup.bak"));

    for (const auto point : {InjectionPoint::Scan, InjectionPoint::End, InjectionPoint::Probe}) {
        for (const auto action : {InjectionAction::Invalidate, InjectionAction::Backup,
                InjectionAction::Writer, InjectionAction::Remount, InjectionAction::External}) {
            arm(point, action);
            const auto before = backupStats;
            assert(installedRefresh());
            disarm();
            assert(backupStats - before >= 73); // first ELF backup short-circuits its sidecar probe
            if (action == InjectionAction::Backup) {
                assert(session.installed.size() == 36);
                for (const auto& app : session.installed) assert(std::strcmp(app.file_name, "example_app_0.elf"));
                assert(Storage.remove("/Apps/example_app_0.elf.BAK"));
            } else assert(manifests() == expected);
            if (action == InjectionAction::Writer) {
                assert(!Storage.generation().quiescent && concurrentWriter.close());
                assert(Storage.remove("/Apps/inventory-writer"));
            }
            if (action == InjectionAction::External) {
                assert(!Storage.generation().quiescent); Storage.externalStorageEnd(true);
                assert(Storage.reconcileExternalStorage());
            }
            assertClean();
            assert(Storage.begin());
            const auto retry = backupStats;
            assert(installedRefresh() && manifests() == expected && backupStats == retry);
            assertClean();
        }
    }
    // Successful recovery without backups still mutates the generation;
    // failed staged recovery must never provide negative-lookup authority.
    assert(Storage.rename("/Apps/example_app_0.json", "/Apps/example_app_0.json.part"));
    noBackups = Storage.generation();
    assert(RuntimePackages::recoverAppInventory(&noBackups) && !noBackups.quiescent);
    assert(Storage.exists("/Apps/example_app_0.json") && !Storage.exists("/Apps/example_app_0.json.part"));
    assert(installedRefresh() && manifests() == expected); assertClean();
    write("/Apps/broken.elf", elfBytes()); write("/Apps/broken.json.part", "invalid");
    noBackups = Storage.generation();
    assert(!RuntimePackages::recoverAppInventory(&noBackups) && !noBackups.quiescent);
    const auto failedRecoveryProbes = backupStats;
    assert(installedRefresh() && manifests() == expected && backupStats - failedRecoveryProbes >= 74);
    assert(Storage.exists("/Apps/broken.elf") && Storage.exists("/Apps/broken.json.part"));
    assert(Storage.remove("/Apps/broken.elf") && Storage.remove("/Apps/broken.json.part"));
    assertClean();
    // Reset even an old valid output stamp on incomplete scans; retain no
    // directory handle or partial negative observation on failures/bounds.
    noBackups = Storage.generation(); failRootRead = true; failAtEntry = 4;
    assert(!RuntimePackages::recoverAppInventory(&noBackups) && !noBackups.quiescent);
    failRootRead = false; assertClean();
    noBackups = Storage.generation(); inventory_sector_ms = 10000;
    assert(!RuntimePackages::recoverAppInventory(&noBackups) && !noBackups.quiescent);
    inventory_sector_ms = 0; assertClean();
    assert(Storage.begin());
    assert(RuntimePackages::recoverAppInventory(&noBackups) && Storage.unchanged(noBackups));
    noBackups = Storage.generation(); syntheticCandidates = true;
    const auto beforeCandidates = nexts;
    assert(!RuntimePackages::recoverAppInventory(&noBackups) && !noBackups.quiescent);
    std::printf("recovery candidate limit next=%u\n", nexts - beforeCandidates);
    assert(nexts - beforeCandidates == 257); syntheticCandidates = false; assertClean();
    noBackups = Storage.generation(); repeatedRootName = "unrelated";
    const auto beforeEntries = nexts;
    assert(!RuntimePackages::recoverAppInventory(&noBackups) && !noBackups.quiescent);
    assert(nexts - beforeEntries == 1025); repeatedRootName = nullptr; assertClean();
    assert(RuntimePackages::recoverAppInventory(&noBackups) && Storage.unchanged(noBackups));
    assert(installedRefresh() && manifests() == expected); assertClean();
    std::puts("Backup observation: exact manifests, case/directory/alias fallback, scan/end/probe invalidation, writer/remount/external, bounds and retry PASS");
#endif
    assert(Storage.mkdir("/Apps/not-a-sidecar.json"));
    t5_app_manifest_t rejected{};
    assert(!readAppManifest("/Apps/not-a-sidecar.json", rejected));
    assert(Storage.rmdir("/Apps/not-a-sidecar.json"));
    // Real managed inspection must win over the loose pair, preserve ID != ELF
    // basename, and reject duplicate valid managed basenames.
    managed("managed-other-id", "example_app_0.elf");
    std::string path; t5_app_manifest_t result{};
    assert(resolveInstalledAppPath("example_app_0.elf", path, &result));
    assert(path == "/sd/Apps/managed-other-id/example_app_0.elf");
    managed("second-id", "example_app_0.elf");
    assert(!resolveInstalledAppPath("example_app_0.elf", path) && path.empty());
    write("/Apps/second-id/example_app_0.elf", "bad");
    assert(resolveInstalledAppPath("example_app_0.elf", path));
    assert(installedRefresh() && session.installed.size() == 38);
    // Recovery still restores interrupted pairs, while mapped executables are
    // left in place until their owning session exits.
    assert(Storage.rename("/Apps/example_app_1.elf", "/Apps/example_app_1.elf.bak"));
    assert(Storage.rename("/Apps/example_app_1.json", "/Apps/example_app_1.json.bak"));
    activePath = "/sd/Apps/example_app_1.elf";
    assert(!RuntimePackages::recoverAppInventory());
    assert(Storage.exists("/Apps/example_app_1.elf.bak") && !Storage.exists("/Apps/example_app_1.elf"));
    activePath = nullptr;
    assert(RuntimePackages::recoverAppInventory() && Storage.exists("/Apps/example_app_1.elf"));
    // Inventory failures after useful entries must not publish their prefix or
    // enter recovery mutations; a later clean attempt still works.
    failRootRead = true; failAtEntry = 4;
    assert(!resolveInstalledAppPath("example_app_36.elf", path) && path.empty());
    assert(!RuntimePackages::recoverAppInventory());
    assert(!installedRefresh() && session.installed.empty());
    failRootRead = false;
    assert(installedRefresh() && session.installed.size() == 38);
    // Both slow progress and the total duration enforce a finite scan.
    inventory_sector_ms = 10000;
    assert(!resolveInstalledAppPath("example_app_36.elf", path));
    assert(!RuntimePackages::recoverAppInventory());
    assert(!installedRefresh() && session.installed.empty());
    inventory_sector_ms = 0;
    assert(hal_delay_calls > 0);
    repeatedRootName = "example_app_0.json";
    const auto beforeAppLimit = nexts;
    assert(!installedRefresh() && session.installed.empty());
    assert(nexts - beforeAppLimit < 1400); // Recovery bound plus 129 app records.
    repeatedRootName = "unrelated";
    assert(!resolveInstalledAppPath("example_app_36.elf", path));
    assert(!RuntimePackages::recoverAppInventory());
    assert(!installedRefresh() && session.installed.empty());
    std::puts("Actual SD/FatFs app traversal: managed priority/ambiguity, validation, read/close failures, deadlines and bounds PASS");
    assertClean();
    assert(driver->quiesce()); driver->stop(); std::free(card_image);
    return 0;
}
