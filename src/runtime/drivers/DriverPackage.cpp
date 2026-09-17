#include "DriverPackage.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <T5DriverApi.h>
#include <T5GnssProvider.h>
#include <mbedtls/sha256.h>
#include "runtime/packages/PackageIdentity.h"

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
    if (!value || std::strlen(value) != 64) return false;
    for (unsigned i = 0; i < 64; ++i) {
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
    return true;
}

bool copyString(const char* value, char* out, size_t capacity) {
    if (!value || !out || capacity == 0) return false;
    const size_t length = std::strlen(value);
    if (!length || length >= capacity) return false;
    std::memcpy(out, value, length + 1);
    return true;
}

// The release catalog and legacy release-manifest path share this parser.
// Log a field name, not untrusted remote JSON content.
bool manifestReject(const char* reason, size_t bytes) {
    LOG_ERR("DRIVER", "Manifest rejected at %s (received=%u bytes)", reason,
            static_cast<unsigned>(bytes));
    return false;
}

bool parseManifest(const std::string& json, JsonDocument& doc, DriverPackageInfo& out) {
    const size_t bytes = json.size();
    if (json.empty()) return manifestReject("empty response", bytes);
    if (bytes > kMaxManifestBytes) return manifestReject("manifest exceeds 4096 bytes", bytes);
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

    // Explicit string conversions are required: `| nullptr` deduces nullptr_t.
    const char* id = view["id"].as<const char*>();
    const char* version = view["version"].as<const char*>();
    const char* sha = view["sha256"].as<const char*>();
    const unsigned size = view["size_bytes"].as<unsigned>();
    REQUIRE_MANIFEST(safeDriverId(id), "id");
    REQUIRE_MANIFEST(version && version[0] && std::strlen(version) < sizeof(out.version), "version");
    // Match applications' shared package namespace and strict semantic version
    // before trusting these values for an on-disk install path or upgrade.
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

// /sd is intentionally a read-only ELF VFS. It supports fopen("rb") but not
// stat, mkdir, write, unlink or rename. All mutations use HalStorage with /Drivers.
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
    // The stage is a newly created, exclusively managed directory. HalStorage
    // closes and syncs its write before this returns.
    if (!Storage.writeFile(storagePath.c_str(), String(value.c_str()))) {
        LOG_ERR("DRIVER", "Unable to write staged manifest: %s", storagePath.c_str());
        return false;
    }
    return true;
}

// Never recursively delete a directory containing unrecognized files.
bool removeManagedDirectory(const std::string& storagePath) {
    if (!Storage.exists(storagePath.c_str())) return true;
    const std::string elf = storagePath + "/driver.elf";
    const std::string manifest = storagePath + "/manifest.json";
    if (Storage.exists(elf.c_str()) && !Storage.remove(elf.c_str())) return false;
    if (Storage.exists(manifest.c_str()) && !Storage.remove(manifest.c_str())) return false;
    if (!Storage.rmdir(storagePath.c_str())) {
        LOG_ERR("DRIVER", "Refusing to remove nonempty/unmanaged directory: %s", storagePath.c_str());
        return false;
    }
    return true;
}

bool directoryIsManagedPackage(const std::string& storagePath, const char* expectedId) {
    if (!Storage.exists((storagePath + "/driver.elf").c_str()) ||
        !Storage.exists((storagePath + "/manifest.json").c_str())) return false;
    std::string json;
    if (!readFile(("/sd" + storagePath + "/manifest.json").c_str(), json)) return false;
    DriverPackageInfo info{};
    return parseDriverPackageManifest(json, info) && std::strcmp(info.id, expectedId) == 0;
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
    const std::string storagePath = std::string("/Drivers/") + id;
    if (!Storage.exists((storagePath + "/driver.elf").c_str())) return false;
    std::string json;
    if (!readFile(("/sd" + storagePath + "/manifest.json").c_str(), json)) return false;
    DriverPackageInfo info{};
    if (!parseDriverPackageManifest(json, info) || std::strcmp(info.id, id) != 0) return false;
    return copyString(info.version, version, capacity);
}

bool installStagedDriverPackage(const std::string& manifestJson, const char* stagedElfVfsPath) {
    // Only the Driver Manager's disposable download may be moved. This
    // prevents a native app from supplying an arbitrary source path.
    constexpr const char* kDownloadedVfs = "/sd/Drivers/.driver-manager.part";
    constexpr const char* kDownloadedStorage = "/Drivers/.driver-manager.part";
    if (!stagedElfVfsPath || std::strcmp(stagedElfVfsPath, kDownloadedVfs) != 0) {
        LOG_ERR("DRIVER", "Install refused: unexpected staged ELF path");
        return false;
    }
    DriverPackageInfo info{};
    LOG_INF("DRIVER", "Install: validating downloaded ELF and manifest");
    if (!validateDriverPayload(manifestJson, stagedElfVfsPath, &info)) {
        LOG_ERR("DRIVER", "Install failed: download/ELF integrity validation");
        return false;
    }
    if (!Storage.ready() || (!Storage.exists("/Drivers") && !Storage.mkdir("/Drivers"))) {
        LOG_ERR("DRIVER", "Install failed: /Drivers storage unavailable");
        return false;
    }

    const std::string target = std::string("/Drivers/") + info.id;
    const std::string stage = std::string("/Drivers/.") + info.id + ".install";
    const std::string backup = std::string("/Drivers/.") + info.id + ".previous";
    const bool hadTarget = Storage.exists(target.c_str());
    if (hadTarget && !directoryIsManagedPackage(target, info.id)) {
        LOG_ERR("DRIVER", "Install refused: unmanaged existing path for %s", info.id);
        return false;
    }
    if (!removeManagedDirectory(stage)) {
        LOG_ERR("DRIVER", "Install failed: stale stage cannot be cleaned");
        return false;
    }
    if (Storage.exists(backup.c_str())) {
        if (!directoryIsManagedPackage(backup, info.id) || !removeManagedDirectory(backup)) {
            LOG_ERR("DRIVER", "Install failed: previous backup is unmanaged or cannot be cleaned");
            return false;
        }
    }
    if (!Storage.mkdir(stage.c_str())) {
        LOG_ERR("DRIVER", "Install failed: cannot create stage %s", stage.c_str());
        return false;
    }

    const std::string stageElf = stage + "/driver.elf";
    const std::string stageManifest = stage + "/manifest.json";
    if (!Storage.rename(kDownloadedStorage, stageElf.c_str())) {
        LOG_ERR("DRIVER", "Install failed: cannot move downloaded ELF into stage");
        (void)removeManagedDirectory(stage);
        return false;
    }
    if (!writeManifest(stageManifest, manifestJson) ||
        !validateDriverPayload(manifestJson, ("/sd" + stageElf).c_str(), nullptr)) {
        LOG_ERR("DRIVER", "Install failed: staged manifest write or integrity recheck");
        (void)removeManagedDirectory(stage);
        return false;
    }
    if (hadTarget && !Storage.rename(target.c_str(), backup.c_str())) {
        LOG_ERR("DRIVER", "Install failed: cannot back up existing driver %s", info.id);
        (void)removeManagedDirectory(stage);
        return false;
    }
    if (!Storage.rename(stage.c_str(), target.c_str())) {
        LOG_ERR("DRIVER", "Install failed: cannot commit staged driver %s", info.id);
        if (hadTarget && !Storage.rename(backup.c_str(), target.c_str())) {
            LOG_ERR("DRIVER", "ROLLBACK FAILED: previous driver preserved at %s", backup.c_str());
        }
        (void)removeManagedDirectory(stage);
        return false;
    }
    if (hadTarget && !removeManagedDirectory(backup)) {
        LOG_ERR("DRIVER", "Installed %s, but previous backup cleanup failed: %s", info.id, backup.c_str());
    }
    LOG_INF("DRIVER", "Installed driver %s %s; activation unchanged", info.id, info.version);
    return true;
}

bool validateGpsDriverPackage() {
    if (native_app_register_sd_vfs() != ESP_OK) return false;
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
