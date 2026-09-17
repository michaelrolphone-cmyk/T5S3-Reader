#include "DriverPackage.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <T5DriverApi.h>
#include <T5GnssProvider.h>
#include <mbedtls/sha256.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

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
    if (!id || !id[0] || std::strlen(id) >= 64) return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(id); *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_') continue;
        return false;
    }
    return std::strstr(id, "..") == nullptr;
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

// The release catalog and the legacy release-manifest path share this parser.
// Report the rejected *field*, not the JSON payload: the latter can contain
// arbitrary server responses and should not be copied verbatim into serial logs.
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

    // ArduinoJson's operator| deduces std::nullptr_t for a nullptr fallback;
    // it does not request a const char* and therefore returns null for strings.
    const char* id = view["id"].as<const char*>();
    const char* version = view["version"].as<const char*>();
    const char* sha = view["sha256"].as<const char*>();
    const unsigned size = view["size_bytes"].as<unsigned>();
    REQUIRE_MANIFEST(safeDriverId(id), "id");
    REQUIRE_MANIFEST(version && version[0] && std::strlen(version) < sizeof(out.version), "version");
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

bool readFile(const char* path, std::string& out) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    char buffer[kMaxManifestBytes + 1];
    const size_t bytes = std::fread(buffer, 1, kMaxManifestBytes + 1, file);
    const bool ok = !std::ferror(file) && bytes > 0 && bytes <= kMaxManifestBytes;
    std::fclose(file);
    if (!ok) return false;
    out.assign(buffer, bytes);
    return true;
}

bool writeFile(const char* path, const std::string& value) {
    FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const bool ok = std::fwrite(value.data(), 1, value.size(), file) == value.size() &&
                    std::fflush(file) == 0 && !std::ferror(file);
    std::fclose(file);
    return ok;
}

bool pathExists(const std::string& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

bool ensureDirectory(const char* path) {
    if (::mkdir(path, 0775) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat info {};
    return ::stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

void removeManagedDirectory(const std::string& path) {
    std::remove((path + "/driver.elf").c_str());
    std::remove((path + "/manifest.json").c_str());
    ::rmdir(path.c_str());
}

bool directoryIsManagedPackage(const std::string& path, const char* expectedId) {
    std::string json;
    if (!readFile((path + "/manifest.json").c_str(), json)) return false;
    DriverPackageInfo info{};
    return parseDriverPackageManifest(json, info) && std::strcmp(info.id, expectedId) == 0 &&
           pathExists(path + "/driver.elf");
}

bool validateElfAndHash(const char* path, unsigned expectedSize, const char* expectedSha) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;

    uint8_t header[20] = {};
    const size_t headerBytes = std::fread(header, 1, sizeof(header), file);
    const bool elfOk = headerBytes == sizeof(header) && header[0] == 0x7f && header[1] == 'E' &&
                       header[2] == 'L' && header[3] == 'F' && header[4] == 1 && header[5] == 1 &&
                       header[16] == 3 && header[17] == 0 && header[18] == 94 && header[19] == 0;
    if (!elfOk || std::fseek(file, 0, SEEK_SET) != 0) {
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

    if (ok) {
        constexpr char hex[] = "0123456789abcdef";
        for (unsigned i = 0; i < 32; ++i) {
            if (expectedSha[i * 2] != hex[digest[i] >> 4] || expectedSha[i * 2 + 1] != hex[digest[i] & 15]) {
                ok = false;
                break;
            }
        }
    }
    return ok;
}
}  // namespace

bool parseDriverPackageManifest(const std::string& json, DriverPackageInfo& out) {
    JsonDocument doc;
    return parseManifest(json, doc, out);
}

bool validateDriverPayload(const std::string& manifestJson, const char* elfVfsPath, DriverPackageInfo* out) {
    if (!elfVfsPath || native_app_register_sd_vfs() != ESP_OK) return false;
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
    const std::string base = std::string("/sd/Drivers/") + id;
    if (!pathExists(base + "/driver.elf")) return false;
    std::string json;
    if (!readFile((base + "/manifest.json").c_str(), json)) return false;
    DriverPackageInfo info{};
    if (!parseDriverPackageManifest(json, info) || std::strcmp(info.id, id) != 0) return false;
    return copyString(info.version, version, capacity);
}

bool installStagedDriverPackage(const std::string& manifestJson, const char* stagedElfVfsPath) {
    DriverPackageInfo info{};
    if (!validateDriverPayload(manifestJson, stagedElfVfsPath, &info)) {
        LOG_ERR("DRIVER", "Rejected staged driver package");
        return false;
    }
    if (!ensureDirectory("/sd/Drivers")) return false;

    const std::string target = std::string("/sd/Drivers/") + info.id;
    const std::string stage = std::string("/sd/Drivers/.") + info.id + ".install";
    const std::string backup = std::string("/sd/Drivers/.") + info.id + ".previous";
    const bool hadTarget = pathExists(target);
    if (hadTarget && !directoryIsManagedPackage(target, info.id)) {
        LOG_ERR("DRIVER", "Refusing to replace unmanaged path for %s", info.id);
        return false;
    }

    removeManagedDirectory(stage);
    if (pathExists(backup)) {
        if (!directoryIsManagedPackage(backup, info.id)) return false;
        removeManagedDirectory(backup);
    }
    if (!ensureDirectory(stage.c_str())) return false;

    const std::string stageElf = stage + "/driver.elf";
    const std::string stageManifest = stage + "/manifest.json";
    std::remove(stageElf.c_str());
    if (std::rename(stagedElfVfsPath, stageElf.c_str()) != 0 || !writeFile(stageManifest.c_str(), manifestJson) ||
        !validateDriverPayload(manifestJson, stageElf.c_str(), nullptr)) {
        removeManagedDirectory(stage);
        return false;
    }

    if (hadTarget && std::rename(target.c_str(), backup.c_str()) != 0) {
        removeManagedDirectory(stage);
        return false;
    }
    if (std::rename(stage.c_str(), target.c_str()) != 0) {
        if (hadTarget) std::rename(backup.c_str(), target.c_str());
        removeManagedDirectory(stage);
        return false;
    }

    if (hadTarget) removeManagedDirectory(backup);
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
