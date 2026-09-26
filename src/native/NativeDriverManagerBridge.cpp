#include <HalStorage.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <T5DriverManagerApi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "runtime/drivers/DriverStageActions.h"
#include "runtime/packages/PackageIdentity.h"
#include "runtime/packages/PackageMutationGate.h"

namespace {
// This ABI is retained ONLY for inspecting and explicitly recovering older
// on-device driver stages. Current discovery, dependency resolution, online
// download and SD/ZIP installation all belong to T5PackageManagerApi.
// No latest-release catalog, loose ELF download or USB-specific normal path
// is reachable through this bridge. Unknown user files must remain intact.
constexpr const char* kDownloadStage = "/Drivers/.driver-manager.part";
constexpr size_t kMaxDriverAssets = 64;

struct RecoveryItem {
    std::string id;
    bool isDownload = false;
};
std::vector<RecoveryItem> recoveryItems;
// The ordinary installer and historical recovery share the same /Drivers
// stage/backup/target paths. A bridge-local lock is NOT transaction isolation.
using ManagerMutation = RuntimePackages::ScopedPackageMutation;

// Privilege is derived from the authenticated execution path, not a package
// catalog row, a proposed driver name, or a manifest supplied by a caller.
bool managerApp() {
    const char* path = native_app_current_path();
    return path && (!std::strcmp(path, "/sd/Apps/driver_manager/driver_manager.elf") ||
                    !std::strcmp(path, "/sd/Apps/driver_manager.elf") ||
                    !std::strcmp(path, "/sd/apps/driver_manager.elf"));
}

bool endsWith(const std::string& value, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return value.size() >= n && value.compare(value.size() - n, n, suffix) == 0;
}

// Append-only v1 compatibility slots must fail closed: applications use the
// common generic package manager API for all current catalog/install work.
bool catalogRefresh() { return false; }
uint32_t catalogCount() { return 0; }
bool catalogGet(uint32_t, t5_driver_catalog_entry_t* out) {
    if (out) *out = {};
    return false;
}
bool installedVersionGet(const char*, char* version, size_t capacity) {
    if (version && capacity) version[0] = '\0';
    return false;
}
bool install(uint32_t) { return false; }
bool installWithProgress(uint32_t, t5_driver_install_progress_t, void*) { return false; }

// Inventory is bounded to manager-owned historical stages. Do not treat a
// similarly named user folder as an executable, and do not delete anything
// during discovery. Stage resolution and retry go through the ordinary
// transaction, not this legacy catalog ABI. Called with the shared gate held.
bool rebuildRecoveryInventory() {
    if (!Storage.ready()) return false;
    std::vector<RecoveryItem> found;
    if (!Storage.exists("/Drivers")) {
        recoveryItems.clear();
        return true;
    }
    HalFile directory = Storage.open("/Drivers", O_RDONLY);
    if (!directory.isOpen() || !directory.isDirectory()) {
        if (directory.isOpen()) (void)directory.close();
        return false;
    }
    bool valid = true;
    while (valid) {
        HalFile entry = directory.openNextFile();
        if (!entry.isOpen()) break;
        char name[128]{};
        const size_t length = entry.getName(name, sizeof(name));
        if (!length || length >= sizeof(name)) {
            valid = false;
        } else if (std::strcmp(name, ".driver-manager.part") == 0) {
            found.push_back({"", true});
        } else {
            const std::string filename(name);
            constexpr const char* suffix = ".pkg-stage";
            if (entry.isDirectory() && filename.size() > std::strlen(suffix) + 1 &&
                filename[0] == '.' && endsWith(filename, suffix)) {
                const std::string id = filename.substr(1, filename.size() - 1 - std::strlen(suffix));
                if (RuntimePackages::safeId(id.c_str())) found.push_back({id, false});
            }
        }
        if (!entry.close()) valid = false;
        if (found.size() > kMaxDriverAssets + 1) valid = false;
    }
    if (!directory.close() || !valid) return false;
    std::sort(found.begin(), found.end(), [](const RecoveryItem& a, const RecoveryItem& b) {
        if (a.isDownload != b.isDownload) return a.isDownload;
        return a.id < b.id;
    });
    recoveryItems.swap(found);
    return true;
}

bool recoveryRefresh() {
    if (!managerApp()) return false;
    ManagerMutation mutation;
    return mutation && rebuildRecoveryInventory();
}

uint32_t recoveryCount() {
    if (!managerApp()) return 0u;
    ManagerMutation mutation;
    return mutation ? static_cast<uint32_t>(recoveryItems.size()) : 0u;
}

bool recoveryGet(uint32_t index, t5_driver_recovery_entry_t* out) {
    if (out) *out = {};
    if (!managerApp() || !out) return false;
    ManagerMutation mutation;
    if (!mutation || index >= recoveryItems.size()) return false;
    const RecoveryItem& item = recoveryItems[index];
    if (item.isDownload) {
        out->kind = T5_DRIVER_RECOVERY_DOWNLOAD;
        out->state = T5_DRIVER_RECOVERY_INCOMPLETE;
        HalFile part = Storage.open(kDownloadStage, O_RDONLY);
        if (!part.isOpen()) return false;
        if (part.isDirectory()) {
            out->state = T5_DRIVER_RECOVERY_REQUIRES_REPAIR;
        } else {
            out->size_bytes = part.fileSize64();
            out->can_discard = true;
        }
        if (!part.close()) out->can_discard = false;
        return true;
    }
    out->kind = T5_DRIVER_RECOVERY_STAGE;
    std::strncpy(out->id, item.id.c_str(), sizeof(out->id) - 1);
    RuntimePackages::OrdinaryStageReview review{};
    if (!RuntimeDrivers::inspectDriverStage(item.id.c_str(), review)) return false;
    std::strncpy(out->candidate_version, review.candidate.version,
                 sizeof(out->candidate_version) - 1);
    using State = RuntimePackages::OrdinaryStageState;
    switch (review.state) {
        case State::Ready:
            out->state = T5_DRIVER_RECOVERY_READY;
            out->can_retry = true;
            out->can_discard = true;
            break;
        case State::InvalidStage:
        case State::InvalidIdentity:
        case State::Missing:
            out->state = T5_DRIVER_RECOVERY_INVALID;
            out->can_discard = review.state == State::InvalidStage;
            break;
        case State::StaleVersion:
            out->state = T5_DRIVER_RECOVERY_STALE;
            out->can_discard = true;
            break;
        case State::InUse:
            out->state = T5_DRIVER_RECOVERY_MAPPED;
            break;
        case State::RecoveryRequired:
        case State::InvalidInstalled:
            out->state = T5_DRIVER_RECOVERY_REQUIRES_REPAIR;
            break;
    }
    return true;
}

bool recoveryRetry(uint32_t index) {
    if (!managerApp()) return false;
    ManagerMutation mutation;
    if (!mutation || index >= recoveryItems.size() || recoveryItems[index].isDownload) return false;
    const std::string id = recoveryItems[index].id;
    const auto result = RuntimeDrivers::retryDriverStage(id.c_str());
    const bool okay = result == RuntimePackages::OrdinaryTransactionResult::Published ||
                      result == RuntimePackages::OrdinaryTransactionResult::CleanupPending;
    if (okay && !rebuildRecoveryInventory()) LOG_ERR("DRVMGR", "Installed stage; recovery refresh failed");
    return okay;
}

bool recoveryDiscard(uint32_t index) {
    if (!managerApp()) return false;
    ManagerMutation mutation;
    if (!mutation || index >= recoveryItems.size()) return false;
    const RecoveryItem item = recoveryItems[index];
    bool okay = false;
    if (item.isDownload) {
        // Only this exact reserved historical filename, after UI confirmation.
        HalFile part = Storage.open(kDownloadStage, O_RDONLY);
        if (!part.isOpen()) return false;
        const bool regular = !part.isDirectory();
        const bool closed = part.close();
        okay = regular && closed && Storage.remove(kDownloadStage);
    } else {
        okay = RuntimeDrivers::discardDriverStage(item.id.c_str()) ==
               RuntimePackages::OrdinaryStageDiscardResult::Discarded;
    }
    if (okay && !rebuildRecoveryInventory()) LOG_ERR("DRVMGR", "Discard succeeded; recovery refresh failed");
    return okay;
}

const t5_driver_manager_api_v1 api = {
    T5_DRIVER_MANAGER_API_VERSION,
    sizeof(t5_driver_manager_api_v1),
    catalogRefresh,
    catalogCount,
    catalogGet,
    installedVersionGet,
    install,
    recoveryRefresh,
    recoveryCount,
    recoveryGet,
    recoveryRetry,
    recoveryDiscard,
    installWithProgress,
};
}  // namespace

extern "C" const t5_driver_manager_api_v1* t5_driver_manager_get_api(uint32_t version) {
    return version == T5_DRIVER_MANAGER_API_VERSION && managerApp() ? &api : nullptr;
}
