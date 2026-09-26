#include <T5PackageManagerApi.h>
#include <HalStorage.h>
#include <NativeAppLauncher.h>
#include "FileAssociationRegistry.h"
#include <cctype>
#include <cstring>
#include <cstdio>
#include <memory>
#include <new>
#include <string>

#include "NativeOnlineOrdinaryCatalog.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageMutationGate.h"
#include "runtime/packages/PackageOrdinaryManifest.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinarySdZipAdapter.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"
#include "runtime/packages/PackageRteZip.h"

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
using Mutation = RuntimePackages::ScopedPackageMutation;

uint32_t availableCapability(const char* name) {
    return RuntimePackages::installedCapabilityVersion(name);
}

bool appPath(const char* current, const char* id) {
    if (!current || !id || !RuntimePackages::safeId(id)) return false;
    const std::string flat = std::string("/sd/Apps/") + id + ".elf";
    const std::string nested = std::string("/sd/Apps/") + id + "/" + id + ".elf";
    return !std::strcmp(current, flat.c_str()) || !std::strcmp(current, nested.c_str());
}

// Privilege follows the authenticated executing application identity, not a
// catalog row, archive filename or package metadata. Canonical managed apps
// run from /sd/Apps/<id>/<artifact>; legacy flat paths remain bounded adapters.
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
    if (appPath(path, "package_manager")) return 4;
    if (appPath(path, "app_store")) return T5_PACKAGE_APPLICATION;
    if (appPath(path, "driver_manager")) return T5_PACKAGE_DRIVER;
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
    return path.size() < 120;
}

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
    return path.size() < 120;
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
    const bool read = file.read(reinterpret_cast<uint8_t*>(buffer.get()),
                                static_cast<size_t>(bytes)) == static_cast<int>(bytes);
    const bool closed = file.close();
    return read && closed &&
        RuntimePackages::parseOrdinaryManifest(buffer.get(), static_cast<size_t>(bytes), plan) &&
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
        if (!length) return true;
        return data && offset <= bytes && length <= bytes - offset &&
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

bool describeIdentity(const RuntimePackages::Identity& identity,
                      t5_package_preview_t* out) {
    if (!out || !permitted(static_cast<uint8_t>(identity.kind))) return false;
    RuntimePackages::OrdinaryTransactionPaths paths{};
    if (!RuntimePackages::ordinaryTransactionPaths(identity.kind, identity.id, paths)) return false;
    *out = {};
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
    }
    out->valid_installation = good ? 1 : 0;
    if (!good || Storage.exists(paths.stage) || Storage.exists(paths.backup) ||
        Storage.exists(paths.removing) || RuntimePackages::systemPackageUseGate().pinned(paths.target))
        return true;
    out->install_allowed = !out->installed_version[0] ||
        RuntimePackages::comparePackageVersions(out->version, out->installed_version) ==
            RuntimePackages::VersionOrder::Newer;
    return true;
}

bool describe(const RuntimePackages::OrdinaryPackagePlan& plan,
              t5_package_preview_t* out) {
    if (!describeIdentity(plan.identity, out)) return false;
    if (!out->valid_installation || !out->install_allowed) return true;
    if (RuntimePackages::preflightOrdinaryPackage(plan, kPolicy, availableCapability) !=
        RuntimePackages::PreflightResult::ReadyForContentVerification)
        out->install_allowed = 0;
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
    if (!preview(folder, &candidate) || !candidate.valid_installation ||
        !candidate.install_allowed || !permitted(candidate.kind)) return false;
    std::string source;
    if (!folderPath(folder, source)) return false;
    const auto result = RuntimePackages::installOrdinaryFromSd(
        source.c_str(), kPolicy, availableCapability);
    const bool okay = result.result == RuntimePackages::OrdinaryInstallResult::Installed;
    if (okay) {
        clearInstalledCache();
        if (candidate.kind == T5_PACKAGE_APPLICATION)
            (void)NativeFileAssociations::rebuild();
    }
    return okay;
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
    const auto result = RuntimePackages::installOrdinaryFromSdZip(
        source.c_str(), kPolicy, availableCapability, &expected);
    return result.result == RuntimePackages::OrdinaryInstallResult::Installed;
}

bool uninstall(uint8_t kind, const char* id) {
    if (!permitted(kind) || !RuntimePackages::safeId(id)) return false;
    Mutation lock;
    if (!lock) return false;
    return RuntimePackages::uninstallOrdinaryFromSd(
        static_cast<RuntimePackages::Kind>(kind), id, kPolicy, availableCapability) ==
        RuntimePackages::OrdinaryTransactionResult::Removed;
}

bool onlineRefresh() {
    if (callerKind() < 0) return false;
    Mutation lock;
    return lock && RuntimeOnlinePackages::Catalog::refresh();
}

uint32_t onlineCount() {
    return RuntimeOnlinePackages::Catalog::count(callerKind());
}

bool onlineGet(uint32_t index, t5_package_catalog_row_t* out) {
    if (out) *out = {};
    if (callerKind() < 0 || !out) return false;
    RuntimePackages::CatalogPackage candidate{};
    char release[64]{};
    if (!RuntimeOnlinePackages::Catalog::selected(callerKind(), index, candidate, release) ||
        !describeIdentity(candidate.identity, &out->package)) return false;
    std::strcpy(out->archive, candidate.archive);
    return true;
}

bool onlineInstall(uint32_t index) {
    if (callerKind() < 0) return false;
    Mutation lock;
    if (!lock) return false;
    RuntimePackages::CatalogPackage candidate{};
    char release[64]{};
    if (!RuntimeOnlinePackages::Catalog::selected(callerKind(), index, candidate, release))
        return false;
    t5_package_preview_t state{};
    if (!describeIdentity(candidate.identity, &state) || !state.valid_installation ||
        !state.install_allowed || !permitted(state.kind)) return false;
    return RuntimeOnlinePackages::OrdinaryZip::install(candidate, release);
}

const t5_package_manager_api_v1 api = {
    T5_PACKAGE_MANAGER_API_VERSION, sizeof(t5_package_manager_api_v1),
    preview, install, uninstall, previewArchive, installArchive,
    onlineRefresh, onlineCount, onlineGet, onlineInstall,
    refreshInstalled, installedCount, installedGet, replacePackage,
};
} // namespace

extern "C" const t5_package_manager_api_v1* t5_package_manager_get_api(uint32_t version) {
    return version == T5_PACKAGE_MANAGER_API_VERSION && callerKind() >= 0 ? &api : nullptr;
}
