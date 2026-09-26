#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <T5DriverManagerApi.h>
#include "NativeOnlineDriverInstall.h"
#include <esp_task_wdt.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"
#include "runtime/drivers/DriverInstallIntake.h"
#include "runtime/drivers/DriverPackage.h"
#include "runtime/drivers/DriverStageActions.h"
#include "runtime/network/NetworkService.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/InstalledCapabilityResolver.h"

namespace {
constexpr const char* kLatestReleaseApi =
    "https://api.github.com/repos/michaelrolphone-cmyk/T5S3-Reader/releases/latest";
constexpr const char* kCanonicalProviderCatalogUrl =
    "https://raw.githubusercontent.com/michaelrolphone-cmyk/T5S3-Reader/release-index/release-index.json";
constexpr const char* kDriverCatalogUrl =
    "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/driver-catalog.json";
constexpr const char* kLatestReleaseDownloadBase =
    "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/";
constexpr const char* kDownloadStage = "/Drivers/.driver-manager.part";
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr size_t kMaxDriverAssets = 64;
constexpr size_t kMaxDriverCatalogBytes = 64 * 1024;
constexpr size_t kMaxReleaseAssetObjectBytes = 8192;

struct ReleaseAsset {
    std::string name;
    std::string url;
    uint64_t size = 0;
};

struct CatalogDriver {
    DriverPackageInfo info{};
    std::string manifest;
    std::string elfUrl;
    std::string canonicalMetadata;
};

struct RecoveryItem {
    std::string id;
    bool isDownload = false;
};

std::vector<CatalogDriver> catalog;
std::vector<RecoveryItem> recoveryItems;

// Catalog refresh, normal installation, and ALL recovery mutations share the
// same reservation. Recovery cannot erase a staged ELF during installation.
std::atomic_flag managerMutation = ATOMIC_FLAG_INIT;
struct ManagerMutation {
    ManagerMutation() : acquired(!managerMutation.test_and_set(std::memory_order_acquire)) {}
    ~ManagerMutation() {
        if (acquired) managerMutation.clear(std::memory_order_release);
    }
    ManagerMutation(const ManagerMutation&) = delete;
    ManagerMutation& operator=(const ManagerMutation&) = delete;
    explicit operator bool() const { return acquired; }
    bool acquired;
};

bool activeNativeApp() {
    const char* path = native_app_current_path();
    return path && path[0];
}

bool endsWith(const std::string& value, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return value.size() >= n && value.compare(value.size() - n, n, suffix) == 0;
}

bool safeDriverAssetName(const char* value) {
    if (!value || !value[0]) return false;
    const size_t length = std::strlen(value);
    if (length >= 160 || !endsWith(value, ".t5driver.elf") || std::strstr(value, "..")) return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '+') continue;
        return false;
    }
    return true;
}

void sortCatalog() {
    std::sort(catalog.begin(), catalog.end(), [](const CatalogDriver& a, const CatalogDriver& b) {
        return std::strcmp(a.info.id, b.info.id) < 0;
    });
}

bool jsonStringField(const std::string& object, const char* field, std::string& value) {
    const std::string key = std::string("\"") + field + "\"";
    size_t p = object.find(key);
    if (p == std::string::npos) return false;
    p = object.find(':', p + key.size());
    if (p == std::string::npos) return false;
    p = object.find('"', p + 1);
    if (p == std::string::npos) return false;
    ++p;
    value.clear();
    bool escaped = false;
    for (; p < object.size(); ++p) {
        const char c = object[p];
        if (escaped) {
            switch (c) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: return false;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            return true;
        } else {
            value.push_back(c);
        }
    }
    return false;
}

uint64_t jsonUintField(const std::string& object, const char* field) {
    const std::string key = std::string("\"") + field + "\"";
    size_t p = object.find(key);
    if (p == std::string::npos) return 0;
    p = object.find(':', p + key.size());
    if (p == std::string::npos) return 0;
    ++p;
    while (p < object.size() && std::isspace(static_cast<unsigned char>(object[p]))) ++p;
    return static_cast<uint64_t>(std::strtoull(object.c_str() + p, nullptr, 10));
}

class DriverReleaseStream final : public Stream {
 public:
    explicit DriverReleaseStream(std::vector<ReleaseAsset>& assets) : assets_(assets) { assets_.clear(); }
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t* buffer, size_t size) override {
        if (!buffer) return 0;
        for (size_t i = 0; i < size; ++i) consume(static_cast<char>(buffer[i]));
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    bool finish() const { return !failed_ && assetsComplete_; }

 private:
    static constexpr const char* kAssetsKey = "\"assets\"";

    void consume(char c) {
        if (failed_ || assetsComplete_) return;
        if (!insideAssets_) {
            if (!assetsKeyFound_) {
                if (c == kAssetsKey[keyMatch_]) {
                    ++keyMatch_;
                    if (kAssetsKey[keyMatch_] == '\0') {
                        assetsKeyFound_ = true;
                        keyMatch_ = 0;
                    }
                } else {
                    keyMatch_ = c == kAssetsKey[0] ? 1u : 0u;
                }
                return;
            }
            if (c == '[') insideAssets_ = true;
            return;
        }
        if (objectDepth_ == 0) {
            if (c == ']') {
                assetsComplete_ = true;
                insideAssets_ = false;
                return;
            }
            if (c != '{') return;
            object_.clear();
            object_.push_back(c);
            objectDepth_ = 1;
            inString_ = false;
            escaped_ = false;
            return;
        }
        if (object_.size() >= kMaxReleaseAssetObjectBytes) {
            failed_ = true;
            return;
        }
        object_.push_back(c);
        if (inString_) {
            if (escaped_) escaped_ = false;
            else if (c == '\\') escaped_ = true;
            else if (c == '"') inString_ = false;
            return;
        }
        if (c == '"') inString_ = true;
        else if (c == '{') ++objectDepth_;
        else if (c == '}' && --objectDepth_ == 0) parseObject();
    }

    void parseObject() {
        ReleaseAsset asset;
        if (jsonStringField(object_, "name", asset.name) &&
            jsonStringField(object_, "browser_download_url", asset.url) &&
            (endsWith(asset.name, ".t5driver.json") || endsWith(asset.name, ".t5driver.elf"))) {
            asset.size = jsonUintField(object_, "size");
            if (assets_.size() < kMaxDriverAssets * 2) assets_.push_back(std::move(asset));
        }
        object_.clear();
    }

    std::vector<ReleaseAsset>& assets_;
    std::string object_;
    size_t keyMatch_ = 0;
    int objectDepth_ = 0;
    bool assetsKeyFound_ = false;
    bool insideAssets_ = false;
    bool assetsComplete_ = false;
    bool inString_ = false;
    bool escaped_ = false;
    bool failed_ = false;
};

bool connectSavedWifi() {
    if (RuntimeNetwork::ready()) return true;
    WIFI_STORE.loadFromFile();
    const WifiCredential* credential = nullptr;
    const std::string last = WIFI_STORE.getLastConnectedSsid();
    if (!last.empty()) credential = WIFI_STORE.findCredential(last);
    if (!credential) {
        const auto& credentials = WIFI_STORE.getCredentials();
        if (!credentials.empty()) credential = &credentials.front();
    }
    if (!credential || credential->ssid.empty()) {
        LOG_ERR("DRVMGR", "No saved Wi-Fi credentials are available");
        return false;
    }

    RuntimeNetwork::wifi().connect(credential->ssid.c_str(),
                                   credential->password.empty() ? nullptr : credential->password.c_str());
    const uint32_t started = millis();
    while (millis() - started < kWifiConnectTimeoutMs) {
        esp_task_wdt_reset();
        const auto state = RuntimeNetwork::state();
        if (state.connection == RuntimeNetwork::ConnectionState::Connected && state.hasAddress) {
            WIFI_STORE.setLastConnectedSsid(credential->ssid);
            return true;
        }
        if (state.connection == RuntimeNetwork::ConnectionState::Failed ||
            state.connection == RuntimeNetwork::ConnectionState::NetworkNotFound) return false;
        delay(100);
    }
    return RuntimeNetwork::ready();
}

bool loadAggregateDriverCatalog() {
    std::string json;
    esp_task_wdt_reset();
    if (!HttpDownloader::fetchUrl(kDriverCatalogUrl, json)) {
        LOG_ERR("DRVMGR", "Aggregate catalog HTTP fetch failed (received=%u bytes)",
                static_cast<unsigned>(json.size()));
        return false;
    }
    if (json.empty() || json.size() > kMaxDriverCatalogBytes) {
        LOG_ERR("DRVMGR", "Aggregate catalog invalid response length: %u bytes",
                static_cast<unsigned>(json.size()));
        return false;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, json);
    if (error) {
        LOG_ERR("DRVMGR", "Aggregate catalog JSON decode failed: %s (%u bytes, first_byte=0x%02x)",
                error.c_str(), static_cast<unsigned>(json.size()),
                static_cast<unsigned>(static_cast<unsigned char>(json[0])));
        return false;
    }
    if (!doc.is<JsonObjectConst>() || doc["schema"] != 1 || !doc["drivers"].is<JsonArrayConst>()) {
        LOG_ERR("DRVMGR", "Aggregate catalog contract invalid: root=%u schema=%u drivers_array=%u",
                static_cast<unsigned>(doc.is<JsonObjectConst>()),
                static_cast<unsigned>(doc["schema"].as<unsigned>()),
                static_cast<unsigned>(doc["drivers"].is<JsonArrayConst>()));
        return false;
    }
    const JsonArrayConst drivers = doc["drivers"].as<JsonArrayConst>();
    if (drivers.size() == 0 || drivers.size() > kMaxDriverAssets) {
        LOG_ERR("DRVMGR", "Driver catalog has an invalid entry count: %u",
                static_cast<unsigned>(drivers.size()));
        return false;
    }

    std::vector<CatalogDriver> loaded;
    loaded.reserve(drivers.size());
    unsigned index = 0;
    for (JsonVariantConst entry : drivers) {
        esp_task_wdt_reset();
        if (!entry.is<JsonObjectConst>() || !entry["manifest"].is<JsonObjectConst>()) {
            LOG_ERR("DRVMGR", "Aggregate catalog entry[%u] is not an object with a manifest object", index);
            return false;
        }
        const char* elfAsset = entry["elf_asset"].as<const char*>();
        if (!safeDriverAssetName(elfAsset)) {
            LOG_ERR("DRVMGR", "Aggregate catalog entry[%u] has invalid ELF asset name", index);
            return false;
        }
        std::string manifest;
        serializeJson(entry["manifest"], manifest);
        DriverPackageInfo info{};
        if (!parseDriverPackageManifest(manifest, info)) {
            LOG_ERR("DRVMGR", "Aggregate catalog entry[%u] manifest rejected (bytes=%u)",
                    index, static_cast<unsigned>(manifest.size()));
            return false;
        }
        const std::string expected = std::string(info.id) + "-" + info.version + ".t5driver.elf";
        if (expected != elfAsset) {
            LOG_ERR("DRVMGR", "Aggregate catalog entry[%u] ELF asset name does not match manifest identity", index);
            return false;
        }
        if (std::any_of(loaded.begin(), loaded.end(), [&](const CatalogDriver& candidate) {
                return std::strcmp(candidate.info.id, info.id) == 0;
            })) {
            LOG_ERR("DRVMGR", "Aggregate catalog entry[%u] duplicates driver ID %s", index, info.id);
            return false;
        }
        CatalogDriver driver;
        driver.info = info;
        driver.manifest = std::move(manifest);
        driver.elfUrl = std::string(kLatestReleaseDownloadBase) + elfAsset;
        loaded.push_back(std::move(driver));
        ++index;
    }
    catalog.swap(loaded);
    sortCatalog();
    LOG_INF("DRVMGR", "Loaded %u drivers from aggregate release catalog",
            static_cast<unsigned>(catalog.size()));
    return true;
}

bool loadLegacyReleaseCatalog() {
    std::vector<ReleaseAsset> assets;
    DriverReleaseStream release(assets);
    esp_task_wdt_reset();
    if (!HttpDownloader::fetchUrl(kLatestReleaseApi, release) || !release.finish()) {
        LOG_ERR("DRVMGR", "Failed to read latest GitHub release driver assets");
        return false;
    }
    bool sawManifest = false;
    bool rejectedCandidate = false;
    for (const auto& manifestAsset : assets) {
        if (!endsWith(manifestAsset.name, ".t5driver.json")) continue;
        sawManifest = true;
        const std::string stem = manifestAsset.name.substr(0, manifestAsset.name.size() - 5);
        const std::string elfName = stem + ".elf";
        const auto elf = std::find_if(assets.begin(), assets.end(), [&](const ReleaseAsset& candidate) {
            return candidate.name == elfName;
        });
        if (elf == assets.end()) {
            LOG_ERR("DRVMGR", "Driver manifest has no ELF companion: %s", manifestAsset.name.c_str());
            rejectedCandidate = true;
            continue;
        }
        std::string manifest;
        DriverPackageInfo info{};
        esp_task_wdt_reset();
        if (!HttpDownloader::fetchUrl(manifestAsset.url, manifest)) {
            LOG_ERR("DRVMGR", "Failed to fetch driver manifest: %s (received=%u expected=%llu bytes)",
                    manifestAsset.name.c_str(), static_cast<unsigned>(manifest.size()),
                    static_cast<unsigned long long>(manifestAsset.size));
            rejectedCandidate = true;
            continue;
        }
        if (!manifestAsset.size || manifest.size() != manifestAsset.size) {
            LOG_ERR("DRVMGR", "Driver manifest length mismatch: %s received=%u expected=%llu bytes",
                    manifestAsset.name.c_str(), static_cast<unsigned>(manifest.size()),
                    static_cast<unsigned long long>(manifestAsset.size));
            rejectedCandidate = true;
            continue;
        }
        if (!parseDriverPackageManifest(manifest, info)) {
            LOG_ERR("DRVMGR", "Rejected driver manifest: %s (received=%u bytes, parser reason above)",
                manifestAsset.name.c_str(), static_cast<unsigned>(manifest.size()));
            rejectedCandidate = true;
            continue;
        }
        if (elf->size != info.sizeBytes) {
            LOG_ERR("DRVMGR", "Driver size mismatch for %s: release=%llu manifest=%u", info.id,
                    static_cast<unsigned long long>(elf->size), static_cast<unsigned>(info.sizeBytes));
            rejectedCandidate = true;
            continue;
        }
        CatalogDriver driver;
        driver.info = info;
        driver.manifest = std::move(manifest);
        driver.elfUrl = elf->url;
        catalog.push_back(std::move(driver));
        if (catalog.size() >= kMaxDriverAssets) break;
    }
    sortCatalog();
    LOG_INF("DRVMGR", "Found %u installable driver packages using legacy release discovery",
            static_cast<unsigned>(catalog.size()));
    return !catalog.empty() || (!sawManifest && !rejectedCandidate);
}


// Canonical release assets are separate from old .t5driver.elf files. A
// physical provider is only advertised when its four-file inventory can be
// fetched and verified by the same transaction used by the SD inbox.
bool loadCanonicalDriverCatalog() {
    std::string json;
    if (!HttpDownloader::fetchUrl(kCanonicalProviderCatalogUrl, json) ||
        json.empty() || json.size() > kMaxDriverCatalogBytes) return false;
    JsonDocument document;
    if (deserializeJson(document, json) || !document.is<JsonObjectConst>() ||
        document["schema"] != 1 || !document["drivers"].is<JsonArrayConst>())
        return false;
    const JsonArrayConst entries = document["drivers"].as<JsonArrayConst>();
    if (entries.size() == 0 || entries.size() > kMaxDriverAssets) return false;
    std::vector<CatalogDriver> found;
    found.reserve(entries.size());
    for (JsonVariantConst entry : entries) {
        if (!entry.is<JsonObjectConst>() || !entry["tag"].is<const char*>() ||
            !entry["manifest"].is<JsonObjectConst>()) return false;
        const JsonObjectConst manifest = entry["manifest"].as<JsonObjectConst>();
        if (!manifest["id"].is<const char*>() ||
            !manifest["version"].is<const char*>() ||
            !manifest["capability"].is<const char*>() ||
            !manifest["api"].is<unsigned>() ||
            !manifest["files"].is<JsonArrayConst>() ||
            !entry["asset"].is<const char*>() || !entry["url"].is<const char*>())
            return false;
        const char* id = manifest["id"].as<const char*>();
        const char* version = manifest["version"].as<const char*>();
        const char* capability = manifest["capability"].as<const char*>();
        const char* tag = entry["tag"].as<const char*>();
        const char* asset = entry["asset"].as<const char*>();
        const char* assetUrl = entry["url"].as<const char*>();
        if (!RuntimePackages::safeId(id) || !RuntimePackages::safeVersion(version) ||
            !RuntimePackages::safePackageCapability(capability) ||
            manifest["api"].as<unsigned>() == 0) return false;
        const std::string expectedTag = std::string("driver-") + id + "-v" + version;
        const std::string expectedAsset = std::string(id) + "--driver.elf";
        const std::string expectedUrl =
            std::string("https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/download/") +
            tag + "/" + asset;
        if (std::strcmp(tag, expectedTag.c_str()) ||
            std::strcmp(asset, expectedAsset.c_str()) ||
            std::strcmp(assetUrl, expectedUrl.c_str())) return false;
        uint32_t parts[3]{};
        if (!RuntimePackages::parsePackageVersion(version, parts)) return false;
        const JsonArrayConst files = manifest["files"].as<JsonArrayConst>();
        if (files.size() != 4) return false;
        bool elf = false, descriptor = false, profile = false, imports = false;
        uint64_t size = 0;
        for (JsonVariantConst file : files) {
            if (!file.is<JsonObjectConst>() || !file["name"].is<const char*>() ||
                !file["size_bytes"].is<uint64_t>() ||
                !file["sha256"].is<const char*>()) return false;
            const char* name = file["name"].as<const char*>();
            const char* digest = file["sha256"].as<const char*>();
            const uint64_t bytes = file["size_bytes"].as<uint64_t>();
            if (!RuntimePackages::validSha256Hex(digest) || !bytes ||
                bytes > 8u * 1024u * 1024u) return false;
            if (!std::strcmp(name, "driver.elf")) { if (elf) return false; elf = true; size = bytes; }
            else if (!std::strcmp(name, ".package.json")) { if (descriptor || bytes > 4096) return false; descriptor = true; }
            else if (!std::strcmp(name, "provider-abi.v1")) { if (profile) return false; profile = true; }
            else if (!std::strcmp(name, "privileged-imports.v1")) { if (imports) return false; imports = true; }
            else return false;
        }
        if (!elf || !descriptor || !profile || !imports || size > UINT32_MAX ||
            std::any_of(found.begin(), found.end(), [id](const CatalogDriver& candidate) {
                return std::strcmp(candidate.info.id, id) == 0;
            })) return false;
        CatalogDriver candidate;
        std::snprintf(candidate.info.id, sizeof(candidate.info.id), "%s", id);
        std::snprintf(candidate.info.version, sizeof(candidate.info.version), "%s", version);
        std::snprintf(candidate.info.capability, sizeof(candidate.info.capability), "%s", capability);
        candidate.info.sizeBytes = static_cast<uint32_t>(size);
        std::string manifestJson;
        serializeJson(manifest, manifestJson);
        JsonDocument metadata;
        if (manifestJson.empty() || deserializeJson(metadata, manifestJson) ||
            !metadata.is<JsonObject>())
            return false;
        metadata["tag"] = tag;
        serializeJson(metadata, candidate.canonicalMetadata);
        if (candidate.canonicalMetadata.empty() || candidate.canonicalMetadata.size() > 8192) return false;
        found.push_back(std::move(candidate));
    }
    catalog.swap(found);
    sortCatalog();
    LOG_INF("DRVMGR", "Discovered %u drivers from the independent release index",
            static_cast<unsigned>(catalog.size()));
    return true;
}

bool catalogRefresh() {
    if (!activeNativeApp()) return false;
    ManagerMutation mutation;
    if (!mutation) return false;
    catalog.clear();
    if (!connectSavedWifi()) return false;
    const bool canonical = loadCanonicalDriverCatalog();
    if (canonical) {
        // The independent release index is the authoritative current driver
        // catalog. Do not immediately open two more TLS connections for the
        // aggregate catalog and latest-release legacy scan after it succeeded.
        // Besides being redundant for current packages, that fan-out used to
        // overlap short-lived HTTP worker reclamation and exhaust contiguous
        // internal heap on ESP32-S3.
        return true;
    }

    // Compatibility discovery remains available only when the independent
    // index itself cannot be loaded, so older releases still have a bounded
    // fallback without penalizing the normal path.
    std::vector<CatalogDriver>().swap(catalog);
    if (loadAggregateDriverCatalog()) return true;
    std::vector<CatalogDriver>().swap(catalog);
    return loadLegacyReleaseCatalog();
}


uint32_t catalogCount() {
    return activeNativeApp() ? static_cast<uint32_t>(catalog.size()) : 0u;
}

bool catalogGet(uint32_t index, t5_driver_catalog_entry_t* out) {
    if (!activeNativeApp() || !out || index >= catalog.size()) return false;
    *out = {};
    const auto& info = catalog[index].info;
    std::strncpy(out->id, info.id, sizeof(out->id) - 1);
    std::strncpy(out->version, info.version, sizeof(out->version) - 1);
    std::strncpy(out->capability, info.capability, sizeof(out->capability) - 1);
    out->size_bytes = info.sizeBytes;
    return true;
}

bool installedVersionGet(const char* id, char* version, size_t capacity) {
    if (!activeNativeApp() || !version || !capacity || !RuntimePackages::safeId(id)) return false;
    version[0] = 0;
    const std::string canonicalPath = std::string("/Drivers/") + id;
    constexpr RuntimePackages::PackageRuntimePolicy policy{
        "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};
    RuntimePackages::Identity observed{};
    if (RuntimePackages::inspectInstalledOrdinarySdDirectory(canonicalPath.c_str(), policy,
            RuntimePackages::installedCapabilityVersion, observed) &&
        observed.kind == RuntimePackages::Kind::Driver &&
        !std::strcmp(observed.id, id)) {
        const size_t length = std::strlen(observed.version);
        if (length >= capacity) return false;
        std::memcpy(version, observed.version, length + 1);
        return true;
    }
    return getInstalledDriverVersion(id, version, capacity);
}

// The callback remains scoped to the synchronous invocation. Dependencies
// are discovered by their declared capability requirements, never from UI IDs.
bool installCanonicalDependencies(size_t index, std::vector<uint8_t>& visiting,
                                  t5_driver_install_progress_t progress, void* context) {
    if (index >= catalog.size() || index >= visiting.size() ||
        catalog[index].canonicalMetadata.empty() || visiting[index] == 1) return false;
    if (visiting[index] == 2) return true;
    visiting[index] = 1;
    const CatalogDriver& selected = catalog[index];
    RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
        selected.info.id, T5_DRIVER_INSTALL_RESOLVING);
    JsonDocument metadata;
    if (deserializeJson(metadata, selected.canonicalMetadata) ||
        !metadata.is<JsonObjectConst>() || !metadata["requires"].is<JsonArrayConst>())
        return false;
    const JsonArrayConst requirements = metadata["requires"].as<JsonArrayConst>();
    if (requirements.size() > RuntimePackages::kMaxPackageRequirements) return false;
    for (JsonVariantConst requirement : requirements) {
        if (!requirement.is<JsonObjectConst>() ||
            !requirement["capability"].is<const char*>() ||
            !requirement["min_api"].is<unsigned>()) return false;
        const char* capability = requirement["capability"].as<const char*>();
        const uint32_t minimumApi = requirement["min_api"].as<unsigned>();
        if (!RuntimePackages::safePackageCapability(capability) || !minimumApi) return false;
        if (RuntimePackages::installedCapabilityVersion(capability) >= minimumApi) continue;
        size_t prerequisite = catalog.size();
        for (size_t candidate = 0; candidate < catalog.size(); ++candidate) {
            if (catalog[candidate].canonicalMetadata.empty() ||
                std::strcmp(catalog[candidate].info.capability, capability)) continue;
            JsonDocument provider;
            if (deserializeJson(provider, catalog[candidate].canonicalMetadata) ||
                !provider["api"].is<unsigned>() ||
                provider["api"].as<unsigned>() < minimumApi) continue;
            prerequisite = candidate;
            break;
        }
        if (prerequisite == catalog.size()) {
            LOG_ERR("DRVMGR", "Required physical provider unavailable: %s", capability);
            return false;
        }
        LOG_INF("DRVMGR", "Installing %s required by %s",
                catalog[prerequisite].info.id, selected.info.id);
        RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
            catalog[prerequisite].info.id, T5_DRIVER_INSTALL_DEPENDENCY);
        if (!installCanonicalDependencies(prerequisite, visiting, progress, context) ||
            RuntimePackages::installedCapabilityVersion(capability) < minimumApi)
            return false;
    }
    RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
        selected.info.id, T5_DRIVER_INSTALL_CHECKING);
    char installed[T5_DRIVER_VERSION_MAX]{};
    if (installedVersionGet(selected.info.id, installed, sizeof(installed))) {
        const auto order = RuntimePackages::comparePackageVersions(selected.info.version, installed);
        if (order == RuntimePackages::VersionOrder::Equal) {
            visiting[index] = 2;
            RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
                selected.info.id, T5_DRIVER_INSTALL_ALREADY_PRESENT);
            return true;
        }
        if (order != RuntimePackages::VersionOrder::Newer) return false;
    }
    if (!RuntimeOnlinePackages::DriverIntake::install(selected.info.id,
            selected.info.version, selected.canonicalMetadata, progress, context)) return false;
    visiting[index] = 2;
    return true;
}

bool installImpl(uint32_t index, t5_driver_install_progress_t progress, void* context) {
    if (!activeNativeApp() || !Storage.ready()) return false;
    ManagerMutation mutation;
    if (!mutation) {
        LOG_ERR("DRVMGR", "Driver installation already in progress");
        return false;
    }
    if (index >= catalog.size()) return false;
    const CatalogDriver selected = catalog[index];
    if (!selected.canonicalMetadata.empty()) {
        char installed[T5_DRIVER_VERSION_MAX]{};
        if (installedVersionGet(selected.info.id, installed, sizeof(installed)) &&
            RuntimePackages::comparePackageVersions(selected.info.version, installed) !=
                RuntimePackages::VersionOrder::Newer) return false;
        std::vector<uint8_t> visiting(catalog.size(), 0);
        return installCanonicalDependencies(index, visiting, progress, context);
    }
    const char* temporaryStoragePath = kDownloadStage;
    const char* temporaryVfsPath = "/sd/Drivers/.driver-manager.part";
    RuntimePackages::OrdinaryTransactionPaths paths{};
    if (!RuntimePackages::ordinaryTransactionPaths(RuntimePackages::Kind::Driver,
                                                   selected.info.id, paths)) return false;
    if (!Storage.exists("/Drivers") && !Storage.mkdir("/Drivers", false)) return false;
    HalFile root = Storage.open("/Drivers", O_RDONLY);
    const bool validRoot = root.isOpen() && root.isDirectory();
    if (root.isOpen()) (void)root.close();
    if (!validRoot) return false;
    if (Storage.exists(temporaryStoragePath)) {
        LOG_ERR("DRVMGR", "Existing download needs inspection: %s", temporaryStoragePath);
        return false;
    }
    RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
        selected.info.id, T5_DRIVER_INSTALL_CHECKING);
    char installed[T5_DRIVER_VERSION_MAX]{};
    const bool hasInstalled = getInstalledDriverVersion(selected.info.id, installed, sizeof(installed));
    const std::string legacyPrefix = std::string("/Drivers/.") + selected.info.id;
    const bool unresolved = !hasInstalled &&
        (Storage.exists(paths.target) || Storage.exists(paths.backup) ||
         Storage.exists(paths.removing) ||
         Storage.exists((legacyPrefix + ".previous").c_str()) ||
         Storage.exists((legacyPrefix + ".install").c_str()));
    const bool pendingStage = Storage.exists(paths.stage) ||
                              Storage.exists((legacyPrefix + ".install").c_str());
    const auto intake = RuntimeDrivers::decideDriverDownload(
        selected.info.version, hasInstalled ? installed : nullptr, unresolved, pendingStage);
    if (intake != RuntimeDrivers::DownloadIntake::Fresh &&
        intake != RuntimeDrivers::DownloadIntake::Upgrade) {
        LOG_ERR("DRVMGR", "Download refused: version or pending generation (%u) for %s",
                static_cast<unsigned>(intake), selected.info.id);
        return false;
    }
    if (RuntimePackages::systemPackageUseGate().pinned(paths.target)) {
        LOG_ERR("DRVMGR", "Download refused: driver is mapped and must be stopped: %s", selected.info.id);
        return false;
    }
    RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
        selected.info.id, T5_DRIVER_INSTALL_METADATA);
    if (!connectSavedWifi()) return false;
    RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
        selected.info.id, T5_DRIVER_INSTALL_DOWNLOADING, "driver.elf", 0, selected.info.sizeBytes);
    const auto result = HttpDownloader::downloadToFile(
        selected.elfUrl, temporaryStoragePath,
        [progress, context, &selected](size_t bytes, size_t) {
            esp_task_wdt_reset();
            RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
                selected.info.id, T5_DRIVER_INSTALL_DOWNLOADING, "driver.elf",
                bytes, selected.info.sizeBytes);
        });
    if (result != HttpDownloader::OK) {
        if (Storage.exists(temporaryStoragePath))
            LOG_ERR("DRVMGR", "Failed download left partial file for inspection: %s", temporaryStoragePath);
        return false;
    }
    RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
        selected.info.id, T5_DRIVER_INSTALL_VERIFYING);
    const bool ok = installStagedDriverPackage(selected.manifest, temporaryVfsPath);
    if (!ok && Storage.exists(temporaryStoragePath) && !Storage.remove(temporaryStoragePath))
        LOG_ERR("DRVMGR", "Could not clean up this invocation's verified download");
    return ok;
}

bool installWithProgress(uint32_t index, t5_driver_install_progress_t progress, void* context) {
    if (!activeNativeApp() || index >= catalog.size()) return false;
    // Copying the ID is bounded. All callback pointers live only for this
    // invocation and the existing mutation lock still owns every write.
    char id[T5_DRIVER_ID_MAX]{};
    std::strncpy(id, catalog[index].info.id, sizeof(id) - 1);
    RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
        id, T5_DRIVER_INSTALL_RESOLVING);
    const bool ok = installImpl(index, progress, context);
    RuntimeOnlinePackages::DriverIntake::emitProgress(progress, context,
        id, ok ? T5_DRIVER_INSTALL_INSTALLED : T5_DRIVER_INSTALL_FAILED);
    return ok;
}

bool install(uint32_t index) { return installWithProgress(index, nullptr, nullptr); }

// Inventory is constructed under the SAME mutation lock as installs. It never
// crawls user paths or treats a matching filename as validated executable.
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
    if (!activeNativeApp()) return false;
    ManagerMutation mutation;
    return mutation && rebuildRecoveryInventory();
}

uint32_t recoveryCount() {
    return activeNativeApp() ? static_cast<uint32_t>(recoveryItems.size()) : 0u;
}

bool recoveryGet(uint32_t index, t5_driver_recovery_entry_t* out) {
    if (!activeNativeApp() || !out || index >= recoveryItems.size()) return false;
    *out = {};
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
    if (!activeNativeApp()) return false;
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
    if (!activeNativeApp()) return false;
    ManagerMutation mutation;
    if (!mutation || index >= recoveryItems.size()) return false;
    const RecoveryItem item = recoveryItems[index];
    bool okay = false;
    if (item.isDownload) {
        // Exact reserved filename only, and only a regular file. The UI must
        // explicitly confirm deletion; no install or refresh ever calls this.
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
    return version == T5_DRIVER_MANAGER_API_VERSION && activeNativeApp() ? &api : nullptr;
}
