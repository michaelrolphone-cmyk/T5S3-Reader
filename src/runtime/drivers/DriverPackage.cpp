#include "DriverPackage.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <T5DriverApi.h>
#include <T5GnssProvider.h>
#include <mbedtls/sha256.h>
#include "runtime/packages/PackageIdentity.h"
#include "runtime/packages/PackageJsonGuard.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"
#include "runtime/packages/PackageTransaction.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr unsigned kMaxDriverBytes = 256u * 1024u;
constexpr size_t kMaxManifestBytes = 4096u;

bool matches(JsonVariantConst value, const char* expected) {
    return value.is<const char*>() && std::strcmp(value.as<const char*>(), expected) == 0;
}

bool capabilityEntry(JsonVariantConst entry) {
    const char* capability = entry["capability"].as<const char*>();
    return capability && capability[0] && std::strlen(capability) < 64 &&
           entry["api"].is<unsigned>() && entry["api"].as<unsigned>() > 0;
}

bool requirement(JsonVariantConst entry, const char* id) {
    return matches(entry["capability"], id) && entry["api"].is<unsigned>() && entry["api"].as<unsigned>() == 1;
}

bool safeDriverId(const char* id) {
    return RuntimePackages::safeId(id);
}

bool validSha256(const char* value) {
    return RuntimePackages::validSha256Hex(value);
}

bool copyString(const char* value, char* out, size_t capacity) {
    if (!value || !out || capacity == 0) return false;
    const size_t length = std::strlen(value);
    if (!length || length >= capacity) return false;
    std::memcpy(out, value, length + 1);
    return true;
}

bool manifestReject(const char* reason, size_t bytes) {
    LOG_ERR("DRIVER", "Manifest rejected at %s (received=%u bytes)", reason,
            static_cast<unsigned>(bytes));
    return false;
}

bool parseManifest(const std::string& json, JsonDocument& doc, DriverPackageInfo& out) {
    const size_t bytes = json.size();
    if (json.empty()) return manifestReject("empty response", bytes);
    if (bytes > kMaxManifestBytes) return manifestReject("manifest exceeds 4096 bytes", bytes);
    if (!RuntimePackages::safePackageJsonObject(json.data(), bytes))
        return manifestReject("duplicate keys or malformed JSON", bytes);
    const DeserializationError error = deserializeJson(doc, json);
    if (error) {
        LOG_ERR("DRIVER", "Manifest rejected at JSON decoding: %s (received=%u bytes, first_byte=0x%02x)",
                error.c_str(), static_cast<unsigned>(bytes),
                static_cast<unsigned>(static_cast<unsigned char>(json[0])));
        return false;
    }
    const JsonDocument& view = doc;
#define REQUIRE_MANIFEST(condition, name) do { if (!(condition)) return manifestReject(name, bytes); } while (false)
    REQUIRE_MANIFEST(view.is<JsonObjectConst>(), "root object");
    REQUIRE_MANIFEST(matches(view["type"], "driver"), "type");
    REQUIRE_MANIFEST(matches(view["architecture"], "xtensa-esp32s3"), "architecture");
    REQUIRE_MANIFEST(matches(view["file_name"], "driver.elf"), "file_name");
    REQUIRE_MANIFEST(view["driver_abi"].is<unsigned>() &&
                     view["driver_abi"].as<unsigned>() == T5_DRIVER_ABI_VERSION, "driver_abi");
    REQUIRE_MANIFEST(view["size_bytes"].is<unsigned>(), "size_bytes type");
    REQUIRE_MANIFEST(view["sha256"].is<const char*>(), "sha256 type");
    REQUIRE_MANIFEST(view["requires"].is<JsonArrayConst>(), "requires array");
    REQUIRE_MANIFEST(view["provides"].is<JsonArrayConst>(), "provides array");
    REQUIRE_MANIFEST(view["provides"].size() != 0, "provides empty");

    const char* id = view["id"].as<const char*>();
    const char* version = view["version"].as<const char*>();
    const char* sha = view["sha256"].as<const char*>();
    const unsigned size = view["size_bytes"].as<unsigned>();
    REQUIRE_MANIFEST(safeDriverId(id), "id");
    REQUIRE_MANIFEST(version && version[0] && std::strlen(version) < sizeof(out.version), "version");
    RuntimePackages::Identity identity{};
    REQUIRE_MANIFEST(RuntimePackages::makeIdentity(RuntimePackages::Kind::Driver,
        id, version, view["file_name"].as<const char*>(), false, &identity), "package identity");
    REQUIRE_MANIFEST(validSha256(sha), "sha256 value");
    REQUIRE_MANIFEST(size >= 52 && size <= kMaxDriverBytes, "size_bytes range");

    unsigned index = 0;
    for (JsonVariantConst entry : view["requires"].as<JsonArrayConst>()) {
        if (!capabilityEntry(entry)) {
            LOG_ERR("DRIVER", "Manifest rejected at requires[%u] capability/api", index);
            return false;
        }
        ++index;
    }
    index = 0;
    for (JsonVariantConst entry : view["provides"].as<JsonArrayConst>()) {
        if (!capabilityEntry(entry)) {
            LOG_ERR("DRIVER", "Manifest rejected at provides[%u] capability/api", index);
            return false;
        }
        ++index;
    }

    out = {};
    REQUIRE_MANIFEST(copyString(id, out.id, sizeof(out.id)), "id output capacity");
    REQUIRE_MANIFEST(copyString(version, out.version, sizeof(out.version)), "version output capacity");
    const JsonArrayConst provided = view["provides"].as<JsonArrayConst>();
    const char* capability = provided[0]["capability"].as<const char*>();
    REQUIRE_MANIFEST(copyString(capability, out.capability, sizeof(out.capability)), "provided capability output capacity");
    out.sizeBytes = size;
#undef REQUIRE_MANIFEST
    return true;
}

// /sd is intentionally a read-only ELF VFS. All mutation uses HalStorage.
bool readFile(const char* vfsPath, std::string& out) {
    FILE* file = std::fopen(vfsPath, "rb");
    if (!file) return false;
    char buffer[kMaxManifestBytes + 1];
    const size_t bytes = std::fread(buffer, 1, sizeof(buffer), file);
    const bool ok = !std::ferror(file) && bytes > 0 && bytes <= kMaxManifestBytes;
    std::fclose(file);
    if (!ok) return false;
    out.assign(buffer, bytes);
    return true;
}

bool writeManifest(const std::string& storagePath, const std::string& value) {
    if (!Storage.writeFile(storagePath.c_str(), String(value.c_str()))) {
        LOG_ERR("DRIVER", "Unable to write staged manifest: %s", storagePath.c_str());
        return false;
    }
    return true;
}

// Inspect first; do not delete even recognizable files if an unknown entry
// would make the directory impossible to remove. Never recursively delete.
bool removeManagedDirectory(const std::string& storagePath) {
    if (!Storage.exists(storagePath.c_str())) return true;
    HalFile directory = Storage.open(storagePath.c_str(), O_RDONLY);
    if (!directory.isOpen() || !directory.isDirectory()) {
        if (directory.isOpen()) directory.close();
        return false;
    }
    while (true) {
        HalFile entry = directory.openNextFile();
        if (!entry.isOpen()) break;
        char name[128]{};
        const size_t length = entry.getName(name, sizeof(name));
        const bool known = length && length < sizeof(name) && !entry.isDirectory() &&
            (std::strcmp(name, "driver.elf") == 0 || std::strcmp(name, "manifest.json") == 0);
        entry.close();
        if (!known) {
            directory.close();
            LOG_ERR("DRIVER", "Refusing to remove directory with unmanaged entries: %s", storagePath.c_str());
            return false;
        }
    }
    directory.close();
    const std::string elf = storagePath + "/driver.elf";
    const std::string manifest = storagePath + "/manifest.json";
    if (Storage.exists(elf.c_str()) && !Storage.remove(elf.c_str())) return false;
    if (Storage.exists(manifest.c_str()) && !Storage.remove(manifest.c_str())) return false;
    if (!Storage.rmdir(storagePath.c_str())) {
        LOG_ERR("DRIVER", "Cannot remove managed directory: %s", storagePath.c_str());
        return false;
    }
    return true;
}

// An inventory check must reject added files and directories, not merely
// accept the presence of the two files named in a legacy manifest.
bool exactLegacyDriverFiles(const char* path) {
    if (!path) return false;
    HalFile directory = Storage.open(path, O_RDONLY);
    if (!directory.isOpen() || !directory.isDirectory()) {
        if (directory.isOpen()) (void)directory.close();
        return false;
    }
    bool sawElf = false, sawManifest = false, valid = true;
    size_t count = 0;
    while (valid) {
        HalFile entry = directory.openNextFile();
        if (!entry.isOpen()) break;
        char name[128]{};
        const size_t length = entry.getName(name, sizeof(name));
        if (!length || length >= sizeof(name) || entry.isDirectory()) {
            valid = false;
        } else if (std::strcmp(name, "driver.elf") == 0 && !sawElf) {
            sawElf = true;
        } else if (std::strcmp(name, "manifest.json") == 0 && !sawManifest) {
            sawManifest = true;
        } else {
            valid = false;
        }
        (void)entry.close();
        ++count;
    }
    const bool closed = directory.close();
    return closed && valid && count == 2 && sawElf && sawManifest;
}

bool validateElfAndHash(const char* path, unsigned expectedSize, const char* expectedSha) {
    FILE* file = std::fopen(path, "rb");
    if (!file) {
        LOG_ERR("DRIVER", "ELF validation: cannot open %s", path);
        return false;
    }
    uint8_t header[20] = {};
    const size_t headerBytes = std::fread(header, 1, sizeof(header), file);
    const bool elfOk = headerBytes == sizeof(header) && header[0] == 0x7f && header[1] == 'E' &&
                       header[2] == 'L' && header[3] == 'F' && header[4] == 1 && header[5] == 1 &&
                       header[16] == 3 && header[17] == 0 && header[18] == 94 && header[19] == 0;
    if (!elfOk || std::fseek(file, 0, SEEK_SET) != 0) {
        LOG_ERR("DRIVER", "ELF validation: invalid header or seek failed: %s", path);
        std::fclose(file);
        return false;
    }
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = mbedtls_sha256_starts_ret(&hash, 0) == 0;
    unsigned total = 0;
    uint8_t buffer[1024];
    uint8_t digest[32] = {};
    size_t bytes = 0;
    while (ok && (bytes = std::fread(buffer, 1, sizeof(buffer), file)) != 0) {
        total += static_cast<unsigned>(bytes);
        if (total > expectedSize) {
            ok = false;
            break;
        }
        ok = mbedtls_sha256_update_ret(&hash, buffer, bytes) == 0;
    }
    ok = ok && !std::ferror(file) && total == expectedSize && mbedtls_sha256_finish_ret(&hash, digest) == 0;
    std::fclose(file);
    mbedtls_sha256_free(&hash);
    if (!ok) {
        LOG_ERR("DRIVER", "ELF validation: length/read/hash failed for %s (read=%u expected=%u)",
                path, total, expectedSize);
        return false;
    }
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        if (expectedSha[i * 2] != hex[digest[i] >> 4] || expectedSha[i * 2 + 1] != hex[digest[i] & 15]) {
            LOG_ERR("DRIVER", "ELF validation: SHA-256 mismatch for %s", path);
            return false;
        }
    }
    return true;
}

struct DriverStorageOps {
    bool exists(const char* path) const { return Storage.exists(path); }
    bool rename(const char* source, const char* destination) const { return Storage.rename(source, destination); }
};

// The existing driver manifest remains the sole retained metadata during the
// compatibility transition. Each verification reopens it and independently
// checks ELF bytes, identity and exact file inventory; no signing receipt or
// claimed SHA-256 alone grants execution rights.
bool verifiedDriverDirectoryIdentity(const char* path, const char* expectedId,
                                     RuntimePackages::Identity& observed) {
    observed = {};
    if (!path || !safeDriverId(expectedId) || !exactLegacyDriverFiles(path)) return false;
    std::string json;
    if (!readFile((std::string("/sd") + path + "/manifest.json").c_str(), json)) return false;
    DriverPackageInfo info{};
    if (!validateDriverPayload(json, (std::string("/sd") + path + "/driver.elf").c_str(), &info) ||
        std::strcmp(info.id, expectedId) != 0) return false;
    return RuntimePackages::makeIdentity(RuntimePackages::Kind::Driver, info.id,
                                        info.version, "driver.elf", false, &observed);
}

bool verifiedDriverDirectory(const char* path, const char* expectedId) {
    RuntimePackages::Identity observed{};
    return verifiedDriverDirectoryIdentity(path, expectedId, observed);
}

bool recoverDriverDirectory(const char* id) {
    if (!safeDriverId(id) || !Storage.ready() || native_app_register_sd_vfs() != ESP_OK) return false;
    const std::string target = std::string("/Drivers/") + id;
    const std::string legacyStage = std::string("/Drivers/.") + id + ".install";
    const std::string legacyBackup = std::string("/Drivers/.") + id + ".previous";
    DriverStorageOps ops;
    const auto verifyLegacy = [id](const char* path) { return verifiedDriverDirectory(path, id); };
    const auto purge = [](const char* path) { return removeManagedDirectory(path); };
    if (Storage.exists(legacyBackup.c_str())) {
        const RuntimePackages::TransactionPaths oldPaths{
            target.c_str(), legacyStage.c_str(), legacyBackup.c_str()};
        if (!RuntimePackages::recoverDirectoryTransaction(ops, oldPaths, verifyLegacy, purge)) {
            LOG_ERR("DRIVER", "Legacy driver transaction requires recovery: %s", id);
            return false;
        }
    }
    // Recover new package generations as well; inventory must not forget a
    // backup because it uses the new .pkg-previous name.
    RuntimePackages::Identity observed{};
    const auto verify = [id](const char* path, RuntimePackages::Identity& value) {
        return verifiedDriverDirectoryIdentity(path, id, value);
    };
    const auto state = RuntimePackages::recoverOrdinaryPackage(ops,
        RuntimePackages::Kind::Driver, id, verify, purge, observed);
    return state == RuntimePackages::OrdinaryTransactionResult::NoInstalledPackage ||
           state == RuntimePackages::OrdinaryTransactionResult::InstalledVerified ||
           state == RuntimePackages::OrdinaryTransactionResult::PreviousRestored ||
           state == RuntimePackages::OrdinaryTransactionResult::Removed;
}
}  // namespace

bool parseDriverPackageManifest(const std::string& json, DriverPackageInfo& out) {
    JsonDocument doc;
    return parseManifest(json, doc, out);
}

bool validateDriverPayload(const std::string& manifestJson, const char* elfVfsPath, DriverPackageInfo* out) {
    if (!elfVfsPath || native_app_register_sd_vfs() != ESP_OK) {
        LOG_ERR("DRIVER", "ELF validation: missing path or read-only SD VFS unavailable");
        return false;
    }
    JsonDocument doc;
    DriverPackageInfo info{};
    if (!parseManifest(manifestJson, doc, info)) return false;
    const char* sha = doc["sha256"].as<const char*>();
    if (!validateElfAndHash(elfVfsPath, info.sizeBytes, sha)) return false;
    if (out) *out = info;
    return true;
}

bool getInstalledDriverVersion(const char* id, char* version, size_t capacity) {
    if (!safeDriverId(id) || !version || capacity == 0 || native_app_register_sd_vfs() != ESP_OK) return false;
    if (!recoverDriverDirectory(id)) return false;
    const std::string storagePath = std::string("/Drivers/") + id;
    if (!Storage.exists((storagePath + "/driver.elf").c_str())) return false;
    RuntimePackages::Identity observed{};
    if (!verifiedDriverDirectoryIdentity(storagePath.c_str(), id, observed)) return false;
    return copyString(observed.version, version, capacity);
}

bool installStagedDriverPackage(const std::string& manifestJson, const char* stagedElfVfsPath) {
    // Only the Driver Manager's disposable download may be moved. Never accept
    // arbitrary app-selected paths or mutate via the read-only /sd VFS.
    constexpr const char* kDownloadedVfs = "/sd/Drivers/.driver-manager.part";
    constexpr const char* kDownloadedStorage = "/Drivers/.driver-manager.part";
    if (!stagedElfVfsPath || std::strcmp(stagedElfVfsPath, kDownloadedVfs) != 0) {
        LOG_ERR("DRIVER", "Install refused: unexpected staged ELF path");
        return false;
    }
    DriverPackageInfo info{};
    if (!validateDriverPayload(manifestJson, stagedElfVfsPath, &info)) {
        LOG_ERR("DRIVER", "Install failed: download/ELF integrity validation");
        return false;
    }
    if (!Storage.ready() || (!Storage.exists("/Drivers") && !Storage.mkdir("/Drivers"))) {
        LOG_ERR("DRIVER", "Install failed: /Drivers storage unavailable");
        return false;
    }
    RuntimePackages::Identity candidate{};
    if (!RuntimePackages::makeIdentity(RuntimePackages::Kind::Driver, info.id,
                                       info.version, "driver.elf", false, &candidate) ||
        !recoverDriverDirectory(info.id)) {
        LOG_ERR("DRIVER", "Install refused: invalid identity or incomplete prior recovery");
        return false;
    }
    RuntimePackages::OrdinaryTransactionPaths paths{};
    if (!RuntimePackages::ordinaryTransactionPaths(candidate.kind, candidate.id, paths))
        return false;
    DriverStorageOps ops;
    const auto verify = [&info](const char* path, RuntimePackages::Identity& observed) {
        return verifiedDriverDirectoryIdentity(path, info.id, observed);
    };
    const auto purge = [](const char* path) { return removeManagedDirectory(path); };

    // Never delete a previous .pkg-stage: it may represent an interrupted
    // publication or contain unknown data. A separate recovery decision owns it.
    if (Storage.exists(paths.stage)) {
        LOG_ERR("DRIVER", "Install refused: unfinished stage requires review: %s", paths.stage);
        return false;
    }
    if (!Storage.mkdir(paths.stage, false)) {
        LOG_ERR("DRIVER", "Install failed: cannot exclusively create stage %s", paths.stage);
        return false;
    }
    const std::string stageElf = std::string(paths.stage) + "/driver.elf";
    const std::string stageManifest = std::string(paths.stage) + "/manifest.json";
    if (!Storage.rename(kDownloadedStorage, stageElf.c_str())) {
        LOG_ERR("DRIVER", "Install failed: cannot move downloaded ELF into stage");
        (void)removeManagedDirectory(paths.stage);
        return false;
    }
    if (!writeManifest(stageManifest, manifestJson)) {
        LOG_ERR("DRIVER", "Install failed: cannot retain staged manifest");
        (void)removeManagedDirectory(paths.stage);
        return false;
    }
    RuntimePackages::Identity observed{};
    if (!verify(paths.stage, observed) ||
        !RuntimePackages::samePackage(candidate, observed) ||
        std::strcmp(candidate.version, observed.version) != 0) {
        LOG_ERR("DRIVER", "Install failed: staged directory changed or failed integrity verification");
        (void)removeManagedDirectory(paths.stage);
        return false;
    }
    // A package transaction does not activate hardware. This re-verifies the
    // candidate, enforces NEWER semver, checks active ELF pins, and restores the
    // old generation if either rename or post-publication validation fails.
    const auto result = RuntimePackages::publishOrdinaryPackage(ops, candidate,
        verify, purge, observed);
    if (result != RuntimePackages::OrdinaryTransactionResult::Published &&
        result != RuntimePackages::OrdinaryTransactionResult::CleanupPending) {
        LOG_ERR("DRIVER", "Install refused: version, mapped driver or recoverable publish error (%u) for %s",
                static_cast<unsigned>(result), info.id);
        return false;
    }
    if (result == RuntimePackages::OrdinaryTransactionResult::CleanupPending)
        LOG_ERR("DRIVER", "Driver committed; prior generation cleanup pending: %s", info.id);
    LOG_INF("DRIVER", "Installed driver %s %s; activation unchanged", info.id, info.version);
    return true;
}

bool validateGpsDriverPackage() {
    if (native_app_register_sd_vfs() != ESP_OK || !recoverDriverDirectory("gps-nmea")) return false;
    std::string json;
    if (!readFile("/sd/Drivers/gps-nmea/manifest.json", json)) {
        LOG_ERR("DRIVER", "GPS package missing: /Drivers/gps-nmea/manifest.json");
        return false;
    }
    DriverPackageInfo info{};
    if (!validateDriverPayload(json, GPS_DRIVER_ELF, &info) || std::strcmp(info.id, "gps-nmea") != 0 ||
        std::strcmp(info.capability, T5_GNSS_CAPABILITY) != 0) {
        LOG_ERR("DRIVER", "GPS package integrity or identity mismatch");
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc["provides"].is<JsonArrayConst>() || doc["provides"].size() != 1 ||
        !requirement(doc["provides"][0], T5_GNSS_CAPABILITY) || !doc["requires"].is<JsonArrayConst>() ||
        doc["requires"].size() != 3) {
        LOG_ERR("DRIVER", "GPS manifest capability contract mismatch");
        return false;
    }
    unsigned requirements = 0;
    for (JsonVariantConst entry : doc["requires"].as<JsonArrayConst>()) {
        const unsigned bit = requirement(entry, "kernel.serial") ? 1u : requirement(entry, "kernel.power") ? 2u :
                             requirement(entry, "kernel.clock") ? 4u : 0u;
        if (!bit || (requirements & bit)) return false;
        requirements |= bit;
    }
    if (requirements != 7u) return false;
    return true;
}
