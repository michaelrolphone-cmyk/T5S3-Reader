#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <T5DriverManagerApi.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"
#include "runtime/drivers/DriverPackage.h"
#include "runtime/network/NetworkService.h"

namespace {
constexpr const char* kLatestReleaseApi =
    "https://api.github.com/repos/michaelrolphone-cmyk/T5S3-Reader/releases/latest";
constexpr const char* kDriverCatalogUrl =
    "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/driver-catalog.json";
constexpr const char* kLatestReleaseDownloadBase =
    "https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/latest/download/";
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
};

std::vector<CatalogDriver> catalog;

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
        const char* elfAsset = entry["elf_asset"] | nullptr;
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
        // The GitHub release API advertises the byte count for every sidecar.
        // A successful HTTP stream is not sufficient to trust a partial body.
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
    // No published driver packages is a valid empty catalog. If GitHub advertised a
    // driver manifest but every candidate was rejected, surface refresh failure so
    // the app retries instead of incorrectly claiming that the release has no drivers.
    return !catalog.empty() || (!sawManifest && !rejectedCandidate);
}

bool catalogRefresh() {
    if (!activeNativeApp()) return false;
    catalog.clear();
    if (!connectSavedWifi()) return false;

    if (loadAggregateDriverCatalog()) return true;
    catalog.clear();
    LOG_INF("DRVMGR", "Aggregate driver catalog unavailable; using legacy release discovery");
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
    return activeNativeApp() && getInstalledDriverVersion(id, version, capacity);
}

bool install(uint32_t index) {
    if (!activeNativeApp() || index >= catalog.size() || !Storage.ready() || !connectSavedWifi()) return false;
    if (!Storage.mkdir("/Drivers") && !Storage.exists("/Drivers")) return false;
    const char* temporaryStoragePath = "/Drivers/.driver-manager.part";
    const char* temporaryVfsPath = "/sd/Drivers/.driver-manager.part";
    Storage.remove(temporaryStoragePath);
    const auto result = HttpDownloader::downloadToFile(
        catalog[index].elfUrl, temporaryStoragePath,
        [](size_t, size_t) { esp_task_wdt_reset(); });
    if (result != HttpDownloader::OK) {
        Storage.remove(temporaryStoragePath);
        return false;
    }
    const bool ok = installStagedDriverPackage(catalog[index].manifest, temporaryVfsPath);
    if (!ok) Storage.remove(temporaryStoragePath);
    return ok;
}

const t5_driver_manager_api_v1 api = {
    T5_DRIVER_MANAGER_API_VERSION,
    sizeof(t5_driver_manager_api_v1),
    catalogRefresh,
    catalogCount,
    catalogGet,
    installedVersionGet,
    install,
};
}  // namespace

extern "C" const t5_driver_manager_api_v1* t5_driver_manager_get_api(uint32_t version) {
    return version == T5_DRIVER_MANAGER_API_VERSION && activeNativeApp() ? &api : nullptr;
}
