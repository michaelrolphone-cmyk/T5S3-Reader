#include <T5PackageManagerApi.h>
#include <HalStorage.h>
#include <NativeAppLauncher.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinarySdZipAdapter.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"
#include "runtime/packages/PackageRteZip.h"

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

uint32_t availableCapability(const char* name) {
    return RuntimePackages::installedCapabilityVersion(name);
}

// ELF callers can only select the package kind explicitly assigned to them.
// This bridge never uses an archive filename to authorize a mutation.
int callerKind() {
    const char* path = native_app_current_path();
    if (!path) return -1;
    if (std::strcmp(path, "/sd/Apps/package_manager.elf") == 0) return 4;
    if (std::strcmp(path, "/sd/Apps/app_store.elf") == 0) return T5_PACKAGE_APPLICATION;
    if (std::strcmp(path, "/sd/Apps/driver_manager.elf") == 0) return T5_PACKAGE_DRIVER;
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

// Only an archive basename may escape the App Store into this manager.
// ZIP internals are independently constrained by RteZip and ordinary preflight.
bool archivePath(const char* basename, std::string& path) {
    path.clear();
    if (!basename) return false;
    size_t length = 0;
    while (basename[length]) {
        const unsigned char c = static_cast<unsigned char>(basename[length]);
        if (++length >= 120 || !((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') ||
            (c == '.' && length > 1 && basename[length - 2] == '.')) return false;
    }
    if (length <= 8 || std::strcmp(basename + length - 8, ".rte.zip")) return false;
    path = std::string(kInbox) + "/" + basename;
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
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[4096]{});
    if (!buffer) { (void)file.close(); return false; }
    const bool read = file.read(reinterpret_cast<uint8_t*>(buffer.get()), bytes) ==
                      static_cast<int>(bytes);
    const bool closed = file.close();
    return read && closed &&
        RuntimePackages::parseOrdinaryManifest(buffer.get(), bytes, plan) &&
        std::strcmp(plan.identity.id, folder) == 0;
}

bool archiveMetadata(const char* name, RuntimePackages::OrdinaryPackagePlan& plan) {
    plan = {};
    std::string path;
    if (!archivePath(name, path) || !Storage.ready()) return false;
    HalFile file = Storage.open(path.c_str(), O_RDONLY);
    if (!file.isOpen() || file.isDirectory()) {
        if (file.isOpen()) (void)file.close();
        return false;
    }
    const uint64_t bytes = file.fileSize64();
    if (bytes < RuntimePackages::kRteZipEocdBytes ||
        bytes > RuntimePackages::kRteZipMaxTotalBytes + 8192u) {
        (void)file.close();
        return false;
    }
    auto read = [&file, bytes](uint64_t offset, uint8_t* data, size_t length) {
        return data && length && offset <= bytes && length <= bytes - offset &&
            file.seek64(offset) && file.read(data, length) == static_cast<int>(length);
    };
    std::unique_ptr<RuntimePackages::RteZipView> zip(
        new (std::nothrow) RuntimePackages::RteZipView{});
    std::unique_ptr<uint8_t[]> manifest(new (std::nothrow) uint8_t[4096]{});
    if (!zip || !manifest) { (void)file.close(); return false; }
    const bool okay = RuntimePackages::inspectRteZip(read, bytes, *zip) ==
                           RuntimePackages::RteZipResult::Ready &&
        RuntimePackages::planRteZip(read, *zip, manifest.get(), 4096, plan) ==
                           RuntimePackages::RteZipResult::Ready;
    const bool closed = file.close();
    return okay && closed;
}

// Preview is a nonmutating manifest/dependency/version assessment. The full
// archive topology, CRC and every SHA are checked again inside installation.
bool describe(const RuntimePackages::OrdinaryPackagePlan& plan,
              t5_package_preview_t* out) {
    const auto& identity = plan.identity;
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
        good = RuntimePackages::verifyOrdinarySdDirectory(paths.target, kPolicy,
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
    if (RuntimePackages::preflightOrdinaryPackage(plan, kPolicy, availableCapability) !=
        RuntimePackages::PreflightResult::ReadyForContentVerification) return true;
    out->install_allowed = !out->installed_version[0] ||
        RuntimePackages::comparePackageVersions(out->version, out->installed_version) ==
            RuntimePackages::VersionOrder::Newer;
    return true;
}

bool preview(const char* folder, t5_package_preview_t* out) {
    if (out) *out = {};
    if (callerKind() < 0 || !out) return false;
    std::unique_ptr<RuntimePackages::OrdinaryPackagePlan> plan(
        new (std::nothrow) RuntimePackages::OrdinaryPackagePlan{});
    return plan && sourceMetadata(folder, *plan) && describe(*plan, out);
}
bool previewArchive(const char* archive, t5_package_preview_t* out) {
    if (out) *out = {};
    if (callerKind() < 0 || !out) return false;
    std::unique_ptr<RuntimePackages::OrdinaryPackagePlan> plan(
        new (std::nothrow) RuntimePackages::OrdinaryPackagePlan{});
    return plan && archiveMetadata(archive, *plan) && describe(*plan, out);
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
    return result.result == RuntimePackages::OrdinaryInstallResult::Installed;
}
bool installArchive(const char* archive) {
    if (callerKind() < 0) return false;
    Mutation lock;
    if (!lock) return false;
    t5_package_preview_t candidate{};
    if (!previewArchive(archive, &candidate) || !candidate.valid_installation ||
        !candidate.install_allowed || !permitted(candidate.kind)) return false;
    std::string source;
    if (!archivePath(archive, source)) return false;
    RuntimePackages::Identity expected{};
    if (!RuntimePackages::makeIdentity(
            static_cast<RuntimePackages::Kind>(candidate.kind), candidate.id,
            candidate.version, candidate.artifact, false, &expected)) return false;
    const auto result = RuntimePackages::installOrdinaryFromSdZip(source.c_str(),
        kPolicy, availableCapability, &expected);
    return result.result == RuntimePackages::OrdinaryInstallResult::Installed;
}
bool uninstall(uint8_t kind, const char* id) {
    if (!permitted(kind) || !RuntimePackages::safeId(id)) return false;
    Mutation lock;
    if (!lock) return false;
    const auto result = RuntimePackages::uninstallOrdinaryFromSd(
        static_cast<RuntimePackages::Kind>(kind), id, kPolicy, availableCapability);
    return result == RuntimePackages::OrdinaryTransactionResult::Removed;
}
const t5_package_manager_api_v1 api = {
    T5_PACKAGE_MANAGER_API_VERSION, sizeof(t5_package_manager_api_v1),
    preview, install, uninstall, previewArchive, installArchive,
};
} // namespace

extern "C" const t5_package_manager_api_v1* t5_package_manager_get_api(uint32_t version) {
    return version == T5_PACKAGE_MANAGER_API_VERSION && callerKind() >= 0 ? &api : nullptr;
}
