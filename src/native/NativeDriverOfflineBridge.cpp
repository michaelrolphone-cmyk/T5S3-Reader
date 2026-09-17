#include <T5DriverOfflineApi.h>
#include <NativeAppLauncher.h>
#include <Logging.h>
#include "runtime/drivers/DriverPackage.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr size_t kMaxEntries = 64;
constexpr size_t kMaxManifest = 4096;
constexpr const char* kInbox = "/sd/Drivers/Inbox";
constexpr const char* kStage = "/sd/Drivers/.driver-manager-offline.part";
struct Local {
    t5_driver_local_entry_t entry{};
    std::string directory;
};
std::vector<Local> inventory;

bool active() {
    const char* path = native_app_current_path();
    return path && path[0];
}
bool safeId(const char* id) {
    if (!id || !id[0] || std::strlen(id) >= T5_DRIVER_ID_MAX) return false;
    for (const char* p = id; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_') continue;
        return false;
    }
    return true;
}
bool isDirectory(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool readManifest(const std::string& path, std::string& data) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char bytes[kMaxManifest + 1];
    const size_t n = std::fread(bytes, 1, sizeof(bytes), f);
    const bool ok = n > 0 && n <= kMaxManifest && !std::ferror(f);
    std::fclose(f);
    if (!ok) return false;
    data.assign(bytes, n);
    return true;
}
void scan(const char* root, uint32_t source) {
    DIR* dir = ::opendir(root);
    if (!dir) return;
    struct dirent* item;
    while (inventory.size() < kMaxEntries && (item = ::readdir(dir))) {
        const char* name = item->d_name;
        if (!safeId(name)) continue;  // Ignores hidden installer/rollback directories.
        if (source == T5_DRIVER_LOCAL_INSTALLED && std::strcmp(name, "Inbox") == 0) continue;
        const std::string path = std::string(root) + "/" + name;
        if (!isDirectory(path)) continue;
        Local local;
        local.directory = path;
        local.entry.source = source;
        std::strncpy(local.entry.id, name, sizeof(local.entry.id) - 1);
        std::string manifest;
        DriverPackageInfo info{};
        if (readManifest(path + "/manifest.json", manifest) &&
            parseDriverPackageManifest(manifest, info) && std::strcmp(info.id, name) == 0) {
            std::strncpy(local.entry.version, info.version, sizeof(local.entry.version) - 1);
            std::strncpy(local.entry.capability, info.capability, sizeof(local.entry.capability) - 1);
            local.entry.size_bytes = info.sizeBytes;
            if (validateDriverPayload(manifest, (path + "/driver.elf").c_str()))
                local.entry.flags |= T5_DRIVER_LOCAL_VALID;
        }
        inventory.push_back(std::move(local));
    }
    ::closedir(dir);
}
bool refresh() {
    if (!active()) return false;
    inventory.clear();
    if (native_app_register_sd_vfs() != ESP_OK) return false;
    scan("/sd/Drivers", T5_DRIVER_LOCAL_INSTALLED);
    scan(kInbox, T5_DRIVER_LOCAL_INBOX);
    std::sort(inventory.begin(), inventory.end(), [](const Local& a, const Local& b) {
        if (a.entry.source != b.entry.source) return a.entry.source < b.entry.source;
        return std::strcmp(a.entry.id, b.entry.id) < 0;
    });
    LOG_INF("DRVMGR", "Offline inventory: %u installed/inbox entries", static_cast<unsigned>(inventory.size()));
    return true;
}
uint32_t count() { return active() ? static_cast<uint32_t>(inventory.size()) : 0u; }
bool get(uint32_t index, t5_driver_local_entry_t* out) {
    if (!active() || !out || index >= inventory.size()) return false;
    *out = inventory[index].entry;
    return true;
}
bool copyElf(const char* source, uint32_t expectedSize) {
    FILE* src = std::fopen(source, "rb");
    if (!src) return false;
    // The staging name is reserved for this provider; remove leftovers from a
    // previous interrupted attempt, then create a fresh exclusive destination.
    (void)std::remove(kStage);
    FILE* dst = std::fopen(kStage, "wb");
    if (!dst) { std::fclose(src); return false; }
    uint8_t buffer[1024];
    size_t total = 0;
    bool ok = true;
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), src)) != 0) {
        total += n;
        if (total > expectedSize || std::fwrite(buffer, 1, n, dst) != n) { ok = false; break; }
    }
    if (std::ferror(src) || total != expectedSize || std::fflush(dst) != 0 || std::ferror(dst)) ok = false;
    std::fclose(src);
    if (std::fclose(dst) != 0) ok = false;
    if (!ok) (void)std::remove(kStage);
    return ok;
}
bool install(uint32_t index) {
    if (!active() || index >= inventory.size()) return false;
    const Local candidate = inventory[index];
    if (candidate.entry.source != T5_DRIVER_LOCAL_INBOX ||
        !(candidate.entry.flags & T5_DRIVER_LOCAL_VALID)) return false;
    std::string manifest;
    DriverPackageInfo info{};
    const std::string elf = candidate.directory + "/driver.elf";
    if (!readManifest(candidate.directory + "/manifest.json", manifest) ||
        !validateDriverPayload(manifest, elf.c_str(), &info) ||
        std::strcmp(info.id, candidate.entry.id) != 0 ||
        !copyElf(elf.c_str(), info.sizeBytes)) return false;
    const bool ok = installStagedDriverPackage(manifest, kStage);
    (void)std::remove(kStage);
    (void)refresh();
    return ok;
}
const t5_driver_offline_api_v1 api = {
    T5_DRIVER_OFFLINE_API_VERSION, sizeof(t5_driver_offline_api_v1),
    refresh, count, get, install
};
}  // namespace
extern "C" const t5_driver_offline_api_v1* t5_driver_offline_get_api(uint32_t version) {
    return version == T5_DRIVER_OFFLINE_API_VERSION && active() ? &api : nullptr;
}
