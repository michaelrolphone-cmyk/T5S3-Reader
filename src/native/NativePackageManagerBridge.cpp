#include <T5PackageManagerApi.h>
#include <HalStorage.h>
#include <NativeAppLauncher.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <memory>
#include <new>
#include <string>

#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"

namespace {
constexpr const char* kInbox = "/Packages/Inbox";
constexpr RuntimePackages::PackageRuntimePolicy kPolicy{
    "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
constexpr uint32_t kMaxInstalledPackages = 128u;
t5_installed_package_t installedPackages[kMaxInstalledPackages]{};
uint32_t installedPackageCount = 0;

void clearInstalledCache() {
    std::memset(installedPackages, 0, sizeof(installedPackages));
    installedPackageCount = 0;
}
std::atomic_flag mutation = ATOMIC_FLAG_INIT;
struct Mutation {
    bool owned = !mutation.test_and_set(std::memory_order_acquire);
    ~Mutation() { if (owned) mutation.clear(std::memory_order_release); }
    explicit operator bool() const { return owned; }
};

// Installed, integrity-verified provider generations satisfy INSTALL dependency
// preflight; this never grants runtime privileges or activates hardware.
uint32_t availableCapability(const char* name) {
    return RuntimePackages::installedCapabilityVersion(name);
}

// The shared manager may install all four ordinary kinds. The App Store and
// Driver Manager are explicitly limited to their own package kind. An ELF
// cannot pass an arbitrary path or impersonate a manager to gain mutations.
bool callerPath(const char* path, const char* id) {
    if (!path || !id) return false;
    char flat[128]{};
    char canonical[192]{};
    const int a = std::snprintf(flat, sizeof(flat), "/sd/Apps/%s.elf", id);
    const int b = std::snprintf(canonical, sizeof(canonical), "/sd/Apps/%s/%s.elf", id, id);
    return a > 0 && b > 0 && static_cast<size_t>(a) < sizeof(flat) &&
        static_cast<size_t>(b) < sizeof(canonical) &&
        (!std::strcmp(path, flat) || !std::strcmp(path, canonical));
}
int callerKind() {
    const char* path = native_app_current_path();
    if (callerPath(path, "package_manager")) return 4;
    if (callerPath(path, "app_store")) return T5_PACKAGE_APPLICATION;
    if (callerPath(path, "driver_manager")) return T5_PACKAGE_DRIVER;
    return -1;
}
bool permitted(uint8_t kind) {
    const int caller = callerKind();
    return kind <= T5_PACKAGE_PROVIDER && (caller == 4 || caller == kind);
}
bool folderPath(const char* folder, std::string& path) {
    path.clear();
    if (!RuntimePackages::safeId(folder)) return false;
    path = std::string(kInbox) + "/" + folder;
    return true;
}
bool sourceMetadata(const char* folder, RuntimePackages::OrdinaryPackagePlan& plan) {
    plan = {};
    std::string path;
    if (!folderPath(folder, path) || !Storage.ready()) return false;
    const std::string filename = path + "/.package.json";
    HalFile file = Storage.open(filename.c_str(), O_RDONLY);
    if (!file.isOpen() || file.isDirectory()) {
        if (file.isOpen()) (void)file.close();
        return false;
    }
    const uint64_t bytes = file.fileSize64();
    if (!bytes || bytes > 4096) { (void)file.close(); return false; }
    // A nested 4096-byte stack buffer plus OrdinaryPackagePlan and the
    // canonical verifier would overflow the Arduino loopTask during preview.
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[4096]{});
    if (!buffer) { (void)file.close(); return false; }
    const bool read = file.read(reinterpret_cast<uint8_t*>(buffer.get()), bytes) ==
                      static_cast<int>(bytes);
    const bool closed = file.close();
    return read && closed &&
        RuntimePackages::parseOrdinaryManifest(buffer.get(), bytes, plan) &&
        std::strcmp(plan.identity.id, folder) == 0;
}
bool refreshInstalled() {
    if (callerKind() != 4 || !Storage.ready()) return false;
    clearInstalledCache();
    struct Root {
        RuntimePackages::Kind kind;
        const char* path;
    };
    static constexpr Root roots[] = {
        {RuntimePackages::Kind::Application, "/Apps"},
        {RuntimePackages::Kind::Driver, "/Drivers"},
        {RuntimePackages::Kind::Service, "/Services"},
        {RuntimePackages::Kind::Provider, "/Providers"},
    };
    for (const auto& root : roots) {
        HalFile directory = Storage.open(root.path, O_RDONLY);
        if (!directory.isOpen() || !directory.isDirectory()) {
            if (directory.isOpen()) (void)directory.close();
            continue;
        }
        while (installedPackageCount < kMaxInstalledPackages) {
            HalFile entry = directory.openNextFile();
            if (!entry.isOpen()) break;
            char name[T5_PACKAGE_ID_MAX]{};
            const size_t length = entry.getName(name, sizeof(name));
            const bool isDirectory = entry.isDirectory();
            (void)entry.close();
            if (!isDirectory || !length || length >= sizeof(name) ||
                !RuntimePackages::safeId(name))
                continue;

            const std::string target = std::string(root.path) + "/" + name;
            const std::string manifest = target + "/.package.json";
            if (!Storage.exists(manifest.c_str())) continue;

            auto& out = installedPackages[installedPackageCount];
            out = {};
            out.kind = static_cast<uint8_t>(root.kind);
            std::strcpy(out.id, name);
            RuntimePackages::Identity observed{};
            if (RuntimePackages::inspectInstalledOrdinarySdDirectory(
                    target.c_str(), kPolicy, availableCapability, observed) &&
                observed.kind == root.kind && !std::strcmp(observed.id, name)) {
                out.valid_installation = 1;
                std::strcpy(out.version, observed.version);
                std::strcpy(out.artifact, observed.artifact);
            }
            ++installedPackageCount;
        }
        (void)directory.close();
        if (installedPackageCount >= kMaxInstalledPackages) break;
    }
    return true;
}
uint32_t installedCount() {
    return callerKind() == 4 ? installedPackageCount : 0u;
}
bool installedGet(uint32_t index, t5_installed_package_t* out) {
    if (callerKind() != 4 || !out || index >= installedPackageCount) return false;
    *out = installedPackages[index];
    return true;
}

bool preview(const char* folder, t5_package_preview_t* out) {
    if (out) *out = {};
    if (callerKind() < 0 || !out) return false;
    std::unique_ptr<RuntimePackages::OrdinaryPackagePlan> plan(
        new (std::nothrow) RuntimePackages::OrdinaryPackagePlan{});
    if (!plan || !sourceMetadata(folder, *plan)) return false;
    const auto& identity = plan->identity;
    if (!permitted(static_cast<uint8_t>(identity.kind))) return false;
    RuntimePackages::OrdinaryTransactionPaths paths{};
    if (!RuntimePackages::ordinaryTransactionPaths(identity.kind, identity.id, paths)) return false;
    out->kind = static_cast<uint8_t>(identity.kind);
    std::strcpy(out->id, identity.id);
    std::strcpy(out->version, identity.version);
    std::strcpy(out->artifact, identity.artifact);
    bool good = true;
    if (Storage.exists(paths.target)) {
        RuntimePackages::Identity installed{};
        good = RuntimePackages::inspectInstalledOrdinarySdDirectory(paths.target, kPolicy,
            availableCapability, installed) && installed.kind == identity.kind &&
            std::strcmp(installed.id, identity.id) == 0;
        if (good) std::strcpy(out->installed_version, installed.version);
        if (!good && identity.kind == RuntimePackages::Kind::Driver) {
            out->valid_installation = 0;
            return true;
        }
    }
    out->valid_installation = good ? 1 : 0;
    if (!good || Storage.exists(paths.stage) || Storage.exists(paths.backup) ||
        Storage.exists(paths.removing) ||
        RuntimePackages::systemPackageUseGate().pinned(paths.target)) return true;
    const auto policy = RuntimePackages::preflightOrdinaryPackage(*plan, kPolicy,
                                                                 availableCapability);
    if (policy != RuntimePackages::PreflightResult::ReadyForContentVerification)
        return true;
    out->install_allowed = !out->installed_version[0] ||
        RuntimePackages::comparePackageVersions(out->version, out->installed_version) ==
            RuntimePackages::VersionOrder::Newer;
    return true;
}
bool install(const char* folder) {
    if (callerKind() < 0) return false;
    Mutation lock;
    if (!lock) return false;
    t5_package_preview_t candidate{};
    if (!preview(folder, &candidate) || !candidate.install_allowed ||
        !permitted(candidate.kind)) return false;
    std::string source;
    if (!folderPath(folder, source)) return false;
    const auto result = RuntimePackages::installOrdinaryFromSd(source.c_str(),
                                                               kPolicy, availableCapability);
    const bool okay = result.result == RuntimePackages::OrdinaryInstallResult::Installed;
    if (okay) clearInstalledCache();
    return okay;
}

bool replacePackage(const char* folder) {
    if (callerKind() != 4) return false;
    std::unique_ptr<RuntimePackages::OrdinaryPackagePlan> plan(
        new (std::nothrow) RuntimePackages::OrdinaryPackagePlan{});
    if (!plan || !sourceMetadata(folder, *plan)) return false;
    const auto& identity = plan->identity;
    RuntimePackages::OrdinaryTransactionPaths paths{};
    if (!RuntimePackages::ordinaryTransactionPaths(identity.kind, identity.id, paths) ||
        !Storage.exists(paths.target) || Storage.exists(paths.stage) ||
        Storage.exists(paths.backup) || Storage.exists(paths.removing) ||
        RuntimePackages::systemPackageUseGate().pinned(paths.target))
        return false;
    RuntimePackages::Identity installed{};
    if (!RuntimePackages::inspectInstalledOrdinarySdDirectory(
            paths.target, kPolicy, availableCapability, installed) ||
        installed.kind != identity.kind || std::strcmp(installed.id, identity.id))
        return false;
    const auto order = RuntimePackages::comparePackageVersions(identity.version, installed.version);
    if (order != RuntimePackages::VersionOrder::Older &&
        order != RuntimePackages::VersionOrder::Newer)
        return false;
    if (RuntimePackages::preflightOrdinaryPackage(*plan, kPolicy, availableCapability) !=
        RuntimePackages::PreflightResult::ReadyForContentVerification)
        return false;

    Mutation lock;
    if (!lock) return false;
    std::string source;
    if (!folderPath(folder, source)) return false;
    const auto result = RuntimePackages::installOrdinaryFromSd(
        source.c_str(), kPolicy, availableCapability, true);
    const bool okay = result.result == RuntimePackages::OrdinaryInstallResult::Installed;
    if (okay) clearInstalledCache();
    return okay;
}
bool uninstall(uint8_t kind, const char* id) {
    if (!permitted(kind) || !RuntimePackages::safeId(id)) return false;
    Mutation lock;
    if (!lock) return false;
    const auto result = RuntimePackages::uninstallOrdinaryFromSd(
        static_cast<RuntimePackages::Kind>(kind), id, kPolicy, availableCapability);
    const bool okay = result == RuntimePackages::OrdinaryTransactionResult::Removed;
    if (okay) clearInstalledCache();
    return okay;
}
const t5_package_manager_api_v1 api = {
    T5_PACKAGE_MANAGER_API_VERSION, sizeof(t5_package_manager_api_v1),
    preview, install, uninstall,
    refreshInstalled, installedCount, installedGet, replacePackage,
};
} // namespace

extern "C" const t5_package_manager_api_v1* t5_package_manager_get_api(uint32_t version) {
    return version == T5_PACKAGE_MANAGER_API_VERSION && callerKind() >= 0 ? &api : nullptr;
}
