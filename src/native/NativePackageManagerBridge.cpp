#include <T5PackageManagerApi.h>
#include <HalStorage.h>
#include <NativeAppLauncher.h>
#include <atomic>
#include <cstring>
#include <string>

#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"

namespace {
constexpr const char* kInbox = "/Packages/Inbox";
constexpr RuntimePackages::PackageRuntimePolicy kPolicy{
    "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
std::atomic_flag mutation = ATOMIC_FLAG_INIT;
struct Mutation {
    bool owned = !mutation.test_and_set(std::memory_order_acquire);
    ~Mutation() { if (owned) mutation.clear(std::memory_order_release); }
    explicit operator bool() const { return owned; }
};

// This is a declaration preflight, never an authorization grant. Until the
// live provider/capability registry exposes a version lookup for manager-side
// dependency resolution, unresolved requirements fail CLOSED rather than
// forging available capabilities. No special cases for USB/I2C or hardware.
uint32_t availableCapability(const char*) { return 0; }
bool activeManager() {
    const char* path = native_app_current_path();
    return path && std::strcmp(path, "/sd/Apps/package_manager.elf") == 0;
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
    char buffer[4096]{};
    const bool read = file.read(reinterpret_cast<uint8_t*>(buffer), bytes) ==
                      static_cast<int>(bytes);
    const bool closed = file.close();
    return read && closed &&
        RuntimePackages::parseOrdinaryManifest(buffer, bytes, plan) &&
        std::strcmp(plan.identity.id, folder) == 0;
}
bool preview(const char* folder, t5_package_preview_t* out) {
    if (out) *out = {};
    if (!activeManager() || !out) return false;
    RuntimePackages::OrdinaryPackagePlan plan{};
    if (!sourceMetadata(folder, plan)) return false;
    const auto& identity = plan.identity;
    RuntimePackages::OrdinaryTransactionPaths paths{};
    if (!RuntimePackages::ordinaryTransactionPaths(identity.kind, identity.id, paths)) return false;
    out->kind = static_cast<uint8_t>(identity.kind);
    std::strcpy(out->id, identity.id);
    std::strcpy(out->version, identity.version);
    std::strcpy(out->artifact, identity.artifact);
    bool good = true;
    if (Storage.exists(paths.target)) {
        RuntimePackages::Identity installed{};
        good = RuntimePackages::verifyOrdinarySdDirectory(paths.target, kPolicy,
            availableCapability, installed) && installed.kind == identity.kind &&
            std::strcmp(installed.id, identity.id) == 0;
        if (good) std::strcpy(out->installed_version, installed.version);
        // Legacy driver upgrades use their separate validated migration path.
        if (!good && identity.kind == RuntimePackages::Kind::Driver) {
            // A legacy layout cannot be treated as a fresh package: refusal
            // until its owner performs explicit migration/recovery.
            out->valid_installation = 0;
            return true;
        }
    }
    out->valid_installation = good ? 1 : 0;
    if (!good || Storage.exists(paths.stage) || Storage.exists(paths.backup) ||
        Storage.exists(paths.removing) ||
        RuntimePackages::systemPackageUseGate().pinned(paths.target)) return true;
    const auto policy = RuntimePackages::preflightOrdinaryPackage(plan, kPolicy,
                                                                 availableCapability);
    if (policy != RuntimePackages::PreflightResult::ReadyForContentVerification)
        return true;
    out->install_allowed = !out->installed_version[0] ||
        RuntimePackages::comparePackageVersions(out->version, out->installed_version) ==
            RuntimePackages::VersionOrder::Newer;
    return true;
}
bool install(const char* folder) {
    if (!activeManager()) return false;
    Mutation lock;
    if (!lock) return false;
    t5_package_preview_t candidate{};
    if (!preview(folder, &candidate) || !candidate.install_allowed) return false;
    std::string source;
    if (!folderPath(folder, source)) return false;
    const auto result = RuntimePackages::installOrdinaryFromSd(source.c_str(),
                                                               kPolicy, availableCapability);
    return result.result == RuntimePackages::OrdinaryInstallResult::Installed;
}
bool uninstall(uint8_t kind, const char* id) {
    if (!activeManager() || kind > T5_PACKAGE_PROVIDER ||
        !RuntimePackages::safeId(id)) return false;
    Mutation lock;
    if (!lock) return false;
    const auto result = RuntimePackages::uninstallOrdinaryFromSd(
        static_cast<RuntimePackages::Kind>(kind), id, kPolicy, availableCapability);
    return result == RuntimePackages::OrdinaryTransactionResult::Removed;
}
const t5_package_manager_api_v1 api = {
    T5_PACKAGE_MANAGER_API_VERSION, sizeof(t5_package_manager_api_v1),
    preview, install, uninstall,
};
} // namespace

extern "C" const t5_package_manager_api_v1* t5_package_manager_get_api(uint32_t version) {
    return version == T5_PACKAGE_MANAGER_API_VERSION && activeManager() ? &api : nullptr;
}
