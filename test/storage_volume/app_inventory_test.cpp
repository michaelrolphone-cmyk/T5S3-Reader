#include "app_inventory_card.inc"
#include <AppManifestRules.h>
#include <Logging.h>
#include <esp_task_wdt.h>
#include "native/AppManifest.h"
#include "native/AppPackageInstaller.h"
#include "native/InstalledAppPath.h"

static const char* activePath;
const char* native_app_current_path() { return activePath; }
struct AppSession { std::vector<t5_app_manifest_t> installed; } session;
static AppSession* current() { return &session; }
#include "app_inventory_host.inc"

static unsigned opens, stats, nexts, closes;
static uint32_t rootHandle;
static bool failRootRead;
static const char* repeatedRootName;
static unsigned closeFailureCountdown;
static size_t rootEntries, failAtEntry;
static uint32_t countedOpen(void* context, const char* path, uint32_t flags) {
    ++opens; return inspectorExt->file_open(context, path, flags);
}
static bool countedStat(void* context, const char* path, uint64_t* size, bool* dir) {
    ++stats; return inspectorApi->stat(context, path, size, dir);
}
static uint32_t countedDirOpen(void* context, const char* path) {
    ++opens;
    const auto h = inspectorApi->dir_open(context, path);
    if (!std::strcmp(path, "/Apps")) { rootHandle = h; rootEntries = 0; }
    return h;
}
static bool countedNext(void* context, uint32_t h, risc_storage_dirent_v1* entry) {
    ++nexts;
    if (h == rootHandle && failRootRead && rootEntries == failAtEntry) return false;
    if (h == rootHandle) {
        ++rootEntries;
        if (repeatedRootName) { *entry = {}; std::strcpy(entry->name, repeatedRootName); return true; }
    }
    return inspectorApi->dir_next(context, h, entry);
}
static bool countedDirClose(void* context, uint32_t h) {
    ++closes;
    if (h == rootHandle && closeFailureCountdown && --closeFailureCountdown == 0) return false;
    return inspectorExt->dir_close_checked(context, h);
}
static bool countedFileClose(void* context, uint32_t h, bool commit) {
    ++closes; return inspectorApi->file_close(context, h, commit);
}
static uint32_t countedError(void* context, uint32_t h, bool dir) {
    if (dir && h == rootHandle && failRootRead && rootEntries == failAtEntry) return 1;
    return inspectorExt->handle_error(context, h, dir);
}
static void write(const std::string& path, const std::string& bytes) {
    auto file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    assert(file && file.write(bytes.data(), bytes.size()) == bytes.size() && file.close());
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
        pair("/Apps", name.c_str());
    }
    pair("/Apps", "springboard.elf");
    for (unsigned i = 0; i < 8; ++i) write("/Apps/notes_" + std::to_string(i), "untouched");
    if (argc > 1) {
        std::string path;
        closeFailureCountdown = !std::strcmp(argv[1], "close-inventory") ? 2 : 1;
        if (!std::strcmp(argv[1], "close-resolve")) assert(!resolveInstalledAppPath("example_app_0.elf", path) && path.empty());
        else if (!std::strcmp(argv[1], "close-recovery")) assert(!RuntimePackages::recoverAppInventory());
        else assert(!installedRefresh() && session.installed.empty());
        assert(!closeFailureCountdown && !Storage.generation().quiescent);
        std::printf("%s: rejected incomplete close PASS\n", argv[1]);
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
        Measurements m; assert(installedRefresh() && session.installed.size() == 37); m.report("springboard-37-apps");
#ifndef APP_INVENTORY_BASELINE
        assert(opens - m.open < 50 && card_reads - m.read < 4500);
#endif
    }
#ifdef APP_INVENTORY_BASELINE
    return 0;
#endif
    inventory_sector_ms = 0;
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
    return 0;
}
