#include "NativeStreamBridge.h"
#include "NativeAppHost.h"
#include "NativeNavigationInput.h"
#include "NativeOnlineAppInstall.h"
#include "runtime/drivers/GpsDriverRuntime.h"
#include "AppCatalogIndex.h"
#include "AppReleaseAssetRules.h"
#include "AppManifest.h"
#include "AppPackageInstaller.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageUseGate.h"
#include "runtime/packages/PackagePreflight.h"
#include <AppManifestRules.h>
#include <ArduinoJson.h>
#include "components/FontAwesomeIcons.h"
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <NativeAppLauncher.h>
#include <T5AppApi.h>
#include <esp_task_wdt.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "MappedInputManager.h"
#include "NativeSettingsBridge.h"
#include "NativeSystemUiBridge.h"
#include "WifiCredentialStore.h"
#include "activities/RenderLock.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "runtime/network/NetworkService.h"

#ifndef CROSSPOINT_COMPAT_VERSION
#define CROSSPOINT_COMPAT_VERSION CROSSPOINT_VERSION
#endif

namespace {
constexpr const char* kLatestReleaseApi =
    "https://api.github.com/repos/michaelrolphone-cmyk/T5S3-Reader/releases/latest";
constexpr const char* kReleaseIndexUrl =
    "https://raw.githubusercontent.com/michaelrolphone-cmyk/T5S3-Reader/release-index/release-index.json";
constexpr const char* kGameBoyRepository = "michaelrolphone-cmyk/T5S3-GameBoy";
constexpr const char* kAggregateAppCatalogName = "app-catalog.json";
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr size_t kMaxCatalogAssets = 128;
constexpr size_t kMaxCatalogBytes = 64 * 1024;
constexpr size_t kMaxManifestBytes = 8 * 1024;
constexpr size_t kMaxReleaseAssetObjectBytes = 8192;

struct ReleaseCatalogAsset {
  std::string name;
  std::string url;
  std::string manifestUrl;
  uint64_t size = 0;
};

struct CatalogAsset {
  std::string name;
  std::string url;
  std::string manifestUrl;
  std::string manifestJson;
  std::string version;
  uint64_t size = 0;
  t5_app_manifest_t manifest{};
  bool manifestValid = false;
};

struct CatalogManifestAsset {
  std::string name;
  std::string url;
};

struct Session {
  GfxRenderer& renderer;
  MappedInputManager& input;
  TaskHandle_t owner;
  HalFile directory;
  std::vector<CatalogAsset> catalog;
  std::vector<t5_app_manifest_t> installed;
  std::string launchPath;
  bool backExitsApp = true;
  bool exiting = false;
  bool presenting = false;
};
Session* session = nullptr;
bool returned = false;
std::string queuedLaunch;
std::string lastLaunchError;
bool homeRequested = false;
bool firmwareActionPending = false;
Session* current() { return session && !session->presenting && session->owner == xTaskGetCurrentTaskHandle() ? session : nullptr; }
int32_t width() { auto* s = current(); return s ? s->renderer.getScreenWidth() : 0; }
int32_t height() { auto* s = current(); return s ? s->renderer.getScreenHeight() : 0; }
void clear() { if (auto* s = current()) s->renderer.clearScreen(); }
void text(int32_t x, int32_t y, const char* value) {
  if (auto* s = current(); s && value) s->renderer.drawText(UI_12_FONT_ID, x, y, value);
}
void rect(int32_t x, int32_t y, int32_t w, int32_t h, bool black) {
  if (auto* s = current(); s && w > 0 && h > 0) s->renderer.fillRect(x, y, w, h, black);
}
void present(bool full) {
  if (auto* s = current()) {
    esp_task_wdt_reset();
    s->renderer.displayBuffer(full ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH);
    esp_task_wdt_reset();
  }
}
struct ServicedFrame {
  GfxRenderer* renderer;
  bool full;
  std::atomic<bool> done{false};
};
void renderServicedFrame(void* opaque) {
  auto* frame = static_cast<ServicedFrame*>(opaque);
  frame->renderer->displayBuffer(frame->full ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  frame->done.store(true, std::memory_order_release);
  // No frame/session access after publication; this firmware task never runs ELF code.
  vTaskDelete(nullptr);
}
bool presentServiced(bool full, void (*service)(void*), void* context) {
  auto* s = current();
  if (!s || !service) return false;
  ServicedFrame frame{&s->renderer, full};
  s->presenting = true;
  if (xTaskCreatePinnedToCore(renderServicedFrame, "app-refresh", 8192, &frame,
                            1, nullptr, xPortGetCoreID()) != pdPASS) {
    s->presenting = false;
    return false;
  }
  // Like present(), join the physical refresh before allowing framebuffer reuse
  // or app unload. Yield every pass; collect input on its authorized owner task.
  while (!frame.done.load(std::memory_order_acquire)) {
    service(context);
    esp_task_wdt_reset();
    vTaskDelay(1);
  }
  s->presenting = false;
  return true;
}
void setBackExitsApp(bool enabled) {
  if (auto* s = current()) s->backExitsApp = enabled;
}
bool poll(t5_app_input_t* out, uint32_t waitMs) {
  auto* s = current();
  if (!s || !out) return false;
  esp_task_wdt_reset();
  delay(std::max(1u, std::min(waitMs, 50u)));
  s->input.update();
  *out = {};
  using Button = MappedInputManager::Button;
  const Button buttons[] = {Button::Back, Button::Confirm, Button::Left, Button::Right, Button::Up, Button::Down};
  for (unsigned i = 0; i < 6; ++i) {
    if (s->input.isPressed(buttons[i])) out->buttons |= 1u << i;
  }
  MappedInputManager::TouchPoint point{};
  out->tapped = s->input.wasTouchTapped(point, s->renderer);
  if (out->tapped) { out->touch_x = point.x; out->touch_y = point.y; }
  if ((s->backExitsApp && s->input.isPressed(Button::Back)) || s->input.isPressed(Button::Power) ||
      s->input.wasTouchHomeButtonPressed()) {
    s->exiting = true;
  }
  if (s->input.isPressed(Button::Power) || s->input.wasTouchHomeButtonPressed()) homeRequested = true;
  out->exit_requested = s->exiting;
  return true;
}
uint32_t clockMs() { return ::millis(); }

const char* storagePath(const char* path) {
  if (!path) return nullptr;
  if (std::strcmp(path, "/sd") == 0) return "/";
  if (std::strncmp(path, "/sd/", 4) == 0) return path + 3;
  return nullptr;
}

bool dirOpen(const char* path) {
  auto* s = current();
  const char* sdPath = storagePath(path);
  if (!s || !sdPath || !Storage.ready()) return false;
  if (s->directory.isOpen()) s->directory.close();
  s->directory = Storage.open(sdPath, O_RDONLY);
  if (!s->directory.isOpen() || !s->directory.isDirectory()) {
    if (s->directory.isOpen()) s->directory.close();
    return false;
  }
  s->directory.rewindDirectory();
  return true;
}

bool dirNext(t5_app_dirent_t* out) {
  auto* s = current();
  if (!s || !out || !s->directory.isOpen() || !s->directory.isDirectory()) return false;
  *out = {};
  HalFile entry = s->directory.openNextFile();
  if (!entry.isOpen()) return false;
  entry.getName(out->name, sizeof(out->name));
  out->name[sizeof(out->name) - 1] = '\0';
  out->is_directory = entry.isDirectory() ? 1u : 0u;
  out->size = out->is_directory ? 0u : entry.fileSize64();
  entry.close();
  return true;
}

void dirClose() {
  if (auto* s = current(); s && s->directory.isOpen()) s->directory.close();
}

bool safeAssetName(const std::string& name) {
  return name.size() < T5_APP_ASSET_NAME_MAX &&
         NativeAppReleaseRules::appElfAssetName(name);
}

bool copyVersion(const std::string& value, char* out, size_t capacity) {
  if (!out || capacity == 0 || value.size() >= capacity) return false;
  std::memcpy(out, value.c_str(), value.size() + 1);
  return true;
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
      continue;
    }
    if (c == '\\') {
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

class CatalogReleaseStream final : public Stream {
 public:
  CatalogReleaseStream(std::vector<ReleaseCatalogAsset>& catalog, std::string& catalogUrl)
      : catalog_(catalog), catalogUrl_(catalogUrl) {
    catalog_.clear();
    catalogUrl_.clear();
  }

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

  bool finish() {
    if (failed_ || !assetsComplete_) return false;

    // A release now contains app ELFs plus independently versioned driver and
    // provider ELFs. Only an ELF with an exact sibling <basename>.json sidecar
    // is an application candidate. Prune everything else before the next TLS
    // handshake so driver packages cannot enter the App Store fallback path
    // and their large CatalogAsset slots do not remain resident.
    size_t write = 0;
    for (size_t read = 0; read < catalog_.size(); ++read) {
      auto& asset = catalog_[read];
      const auto expected = asset.name.substr(0, asset.name.size() - 4) + ".json";
      const auto manifest = std::find_if(manifests_.begin(), manifests_.end(),
          [&](const CatalogManifestAsset& candidate) { return candidate.name == expected; });
      if (manifest == manifests_.end()) continue;
      asset.manifestUrl = manifest->url;
      if (write != read) catalog_[write] = std::move(asset);
      ++write;
    }
    catalog_.resize(write);
    catalog_.shrink_to_fit();
    return true;
  }

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
      if (escaped_) {
        escaped_ = false;
      } else if (c == '\\') {
        escaped_ = true;
      } else if (c == '"') {
        inString_ = false;
      }
      return;
    }

    if (c == '"') {
      inString_ = true;
    } else if (c == '{') {
      ++objectDepth_;
    } else if (c == '}') {
      --objectDepth_;
      if (objectDepth_ == 0) parseObject();
    }
  }

  void parseObject() {
    ReleaseCatalogAsset asset;
    if (!jsonStringField(object_, "name", asset.name) ||
        !jsonStringField(object_, "browser_download_url", asset.url)) {
      object_.clear();
      return;
    }
    asset.size = jsonUintField(object_, "size");
    if (asset.name == kAggregateAppCatalogName) {
      catalogUrl_ = asset.url;
    } else if (safeAssetName(asset.name)) {
      if (catalog_.size() < kMaxCatalogAssets) catalog_.push_back(std::move(asset));
    } else if (asset.name.size() > 5 && asset.name.substr(asset.name.size() - 5) == ".json") {
      if (manifests_.size() < kMaxCatalogAssets)
        manifests_.push_back(CatalogManifestAsset{std::move(asset.name), std::move(asset.url)});
    }
    object_.clear();
  }

  std::vector<ReleaseCatalogAsset>& catalog_;
  std::string& catalogUrl_;
  std::vector<CatalogManifestAsset> manifests_;
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

void sortCatalog(std::vector<CatalogAsset>& catalog) {
  std::sort(catalog.begin(), catalog.end(), [](const CatalogAsset& a, const CatalogAsset& b) {
    return std::strcmp(a.manifest.display_name, b.manifest.display_name) < 0;
  });
}

bool loadAggregateCatalog(const std::vector<ReleaseCatalogAsset>& releaseAssets,
                          std::vector<CatalogAsset>& catalog,
                          const std::string& catalogUrl) {
  std::vector<std::string> manifests;
  if (!fetchAppCatalogIndex(catalogUrl, manifests)) return false;

  std::vector<CatalogAsset> validated;
  validated.reserve(manifests.size());
  for (auto& json : manifests) {
    esp_task_wdt_reset();
    std::string version;
    t5_app_manifest_t manifest{};
    if (!parseAppManifest(json, manifest, &version, true)) {
      LOG_ERR("APPSTORE", "Aggregate app manifest %u failed validation (file=%s)",
              static_cast<unsigned>(validated.size() + 1), manifest.file_name);
      return false;
    }
    // The parsed manifest is copied into the final catalog below; discard its
    // serialized JSON now instead of retaining every app's raw metadata.
    std::string().swap(json);

    const auto asset = std::find_if(releaseAssets.begin(), releaseAssets.end(),
        [&](const ReleaseCatalogAsset& candidate) {
          return candidate.name == manifest.file_name;
        });
    if (asset == releaseAssets.end()) {
      LOG_ERR("APPSTORE", "Aggregate app %s has no matching release ELF", manifest.file_name);
      return false;
    }
    if (std::any_of(validated.begin(), validated.end(), [&](const CatalogAsset& candidate) {
          return candidate.name == asset->name;
        })) {
      LOG_ERR("APPSTORE", "Aggregate catalog repeats app %s", manifest.file_name);
      return false;
    }

    CatalogAsset resolved;
    resolved.name = asset->name;
    resolved.url = asset->url;
    resolved.manifestUrl = asset->manifestUrl;
    resolved.size = asset->size;
    resolved.manifest = manifest;
    resolved.version = std::move(version);
    resolved.manifestValid = true;
    validated.push_back(std::move(resolved));
  }
  if (validated.empty()) return false;
  sortCatalog(validated);
  catalog.swap(validated);
  return true;
}

bool loadCatalogManifests(std::vector<ReleaseCatalogAsset>& releaseAssets,
                          std::vector<CatalogAsset>& catalog) {
  std::vector<CatalogAsset> validated;
  validated.reserve(releaseAssets.size());
  for (auto& asset : releaseAssets) {
    esp_task_wdt_reset();
    if (asset.manifestUrl.empty()) continue;

    std::string json;
    std::string version;
    t5_app_manifest_t manifest{};
    const bool fetched = HttpDownloader::fetchUrl(asset.manifestUrl, json);
    // Reclaim the metadata worker before either JSON parsing or the next TLS
    // connection. Without this, repeated per-app fallback requests can overlap
    // an idle-task-delayed FreeRTOS task deletion and fragment internal heap.
    delay(1);
    if (!fetched) {
      LOG_ERR("APPSTORE", "Fallback manifest download failed: %s", asset.manifestUrl.c_str());
      continue;
    }
    if (!parseAppManifest(json, manifest, &version, true)) {
      LOG_ERR("APPSTORE", "Fallback manifest validation failed: %s", asset.name.c_str());
      continue;
    }
    if (asset.name != manifest.file_name) {
      LOG_ERR("APPSTORE", "Fallback filename mismatch: asset=%s manifest=%s",
              asset.name.c_str(), manifest.file_name);
      continue;
    }

    CatalogAsset resolved;
    resolved.name = std::move(asset.name);
    resolved.url = std::move(asset.url);
    resolved.manifestUrl = std::move(asset.manifestUrl);
    resolved.size = asset.size;
    resolved.manifest = manifest;
    resolved.version = std::move(version);
    resolved.manifestValid = true;
    validated.push_back(std::move(resolved));
  }
  sortCatalog(validated);
  catalog.swap(validated);
  return true;
}

bool validThirdPartyReleaseTag(const char* tag) {
  if (!tag || tag[0] != 'v') return false;
  unsigned dots = 0;
  bool digit = false;
  for (const char* cursor = tag + 1; *cursor; ++cursor) {
    if (*cursor >= '0' && *cursor <= '9') {
      digit = true;
    } else if (*cursor == '.' && digit && dots < 2) {
      ++dots;
      digit = false;
    } else {
      return false;
    }
  }
  return dots == 2 && digit;
}

bool loadIndependentAppIndex(std::vector<CatalogAsset>& catalog) {
  std::string json;
  esp_task_wdt_reset();
  if (!HttpDownloader::fetchUrl(kReleaseIndexUrl, json) ||
      json.empty() || json.size() > kMaxCatalogBytes) return false;
  JsonDocument document;
  if (deserializeJson(document, json) || !document.is<JsonObjectConst>() ||
      document["schema"] != 1 || !document["apps"].is<JsonArrayConst>()) return false;
  const JsonArrayConst entries = document["apps"].as<JsonArrayConst>();
  if (entries.size() == 0 || entries.size() > kMaxCatalogAssets) return false;

  std::vector<CatalogAsset> indexed;
  indexed.reserve(entries.size());
  for (JsonVariantConst entry : entries) {
    esp_task_wdt_reset();
    if (!entry.is<JsonObjectConst>() || !entry["id"].is<const char*>() ||
        !entry["version"].is<const char*>() || !entry["tag"].is<const char*>() ||
        !entry["asset"].is<const char*>() || !entry["url"].is<const char*>() ||
        !entry["size"].is<uint64_t>() || !entry["sha256"].is<const char*>() ||
        !entry["manifest"].is<JsonObjectConst>()) return false;
    CatalogAsset asset;
    asset.name = entry["asset"].as<const char*>();
    const char* id = entry["id"].as<const char*>();
    const char* version = entry["version"].as<const char*>();
    const char* tag = entry["tag"].as<const char*>();
    const char* url = entry["url"].as<const char*>();
    const char* digest = entry["sha256"].as<const char*>();
    JsonVariantConst sourceRepoValue = entry["source_repo"];
    if (!sourceRepoValue.isNull() && !sourceRepoValue.is<const char*>()) return false;
    const char* sourceRepo = sourceRepoValue.is<const char*>() ?
        sourceRepoValue.as<const char*>() : "";
    asset.size = entry["size"].as<uint64_t>();
    if (!safeAssetName(asset.name) || asset.name.size() <= 4 ||
        asset.name.substr(asset.name.size() - 4) != ".elf" ||
        !RuntimePackages::validSha256Hex(digest) ||
        asset.size < 52 || asset.size > 8u * 1024u * 1024u) return false;
    const std::string expectedId = asset.name.substr(0, asset.name.size() - 4);
    const bool gameBoyProvider = !std::strcmp(id, "gameboy") &&
        !std::strcmp(sourceRepo, kGameBoyRepository);
    if (sourceRepo[0] && !gameBoyProvider) return false;
    std::string expectedTag;
    std::string releaseRepository = "michaelrolphone-cmyk/T5S3-Reader";
    if (gameBoyProvider) {
      if (!validThirdPartyReleaseTag(tag)) return false;
      expectedTag = tag;
      releaseRepository = kGameBoyRepository;
    } else {
      expectedTag = std::string("app-") + expectedId + "-v" + version;
    }
    const std::string expectedUrl = "https://github.com/" + releaseRepository +
        "/releases/download/" + tag + "/" + asset.name;
    if (std::strcmp(id, expectedId.c_str()) || std::strcmp(tag, expectedTag.c_str()) ||
        std::strcmp(url, expectedUrl.c_str())) return false;
    serializeJson(entry["manifest"], asset.manifestJson);
    std::string parsedVersion;
    if (asset.manifestJson.empty() || asset.manifestJson.size() > kMaxManifestBytes ||
        !parseAppManifest(asset.manifestJson, asset.manifest, &parsedVersion, true) ||
        !asset.manifest.compatible || asset.manifest.file_name != asset.name ||
        parsedVersion != version) return false;
    JsonDocument sidecar;
    if (deserializeJson(sidecar, asset.manifestJson) || !sidecar.is<JsonObjectConst>() ||
        !sidecar["sha256"].is<const char*>() || !sidecar["size_bytes"].is<uint64_t>() ||
        sidecar["size_bytes"].as<uint64_t>() != asset.size ||
        std::strcmp(sidecar["sha256"].as<const char*>(), digest)) return false;
    asset.version = version;
    asset.manifestValid = true;
    if (std::any_of(indexed.begin(), indexed.end(), [&](const CatalogAsset& existing) {
          return existing.name == asset.name;
        })) return false;
    indexed.push_back(std::move(asset));
  }
  sortCatalog(indexed);
  catalog.swap(indexed);
  return true;
}

bool connectSavedWifi() {
  if (RuntimeNetwork::ready()) return true;

  WIFI_STORE.loadFromFile();
  const WifiCredential* cred = nullptr;
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) cred = WIFI_STORE.findCredential(last);
  if (!cred) {
    const auto& credentials = WIFI_STORE.getCredentials();
    if (!credentials.empty()) cred = &credentials.front();
  }
  if (!cred || cred->ssid.empty()) {
    LOG_ERR("APPSTORE", "No saved Wi-Fi credentials are available");
    return false;
  }

  LOG_INF("APPSTORE", "Connecting to saved Wi-Fi: %s", cred->ssid.c_str());
  RuntimeNetwork::wifi().connect(cred->ssid.c_str(), cred->password.empty() ? nullptr : cred->password.c_str());

  const uint32_t started = millis();
  while (millis() - started < kWifiConnectTimeoutMs) {
    esp_task_wdt_reset();
    const auto state = RuntimeNetwork::state();
    if (state.connection == RuntimeNetwork::ConnectionState::Connected && state.hasAddress) {
      WIFI_STORE.setLastConnectedSsid(cred->ssid);
      LOG_INF("APPSTORE", "Wi-Fi ready: %s", state.address);
      return true;
    }
    if (state.connection == RuntimeNetwork::ConnectionState::Failed ||
        state.connection == RuntimeNetwork::ConnectionState::NetworkNotFound) {
      LOG_ERR("APPSTORE", "Saved Wi-Fi connection failed before IP assignment");
      return false;
    }
    delay(100);
  }
  LOG_ERR("APPSTORE", "Timed out waiting for saved Wi-Fi and IP address");
  return RuntimeNetwork::ready();
}

bool appCatalogRefresh() {
  auto* s = current();
  if (!s) return false;
  // A refresh invalidates every prior release record. Free the backing storage,
  // not just the elements, so stale catalog capacity is not carried into the
  // next TLS handshake on a memory-constrained ESP32-S3.
  std::vector<CatalogAsset>().swap(s->catalog);
  if (!connectSavedWifi()) return false;

  if (loadIndependentAppIndex(s->catalog)) {
    LOG_INF("APPSTORE", "Loaded %u apps from the independent release index",
            static_cast<unsigned>(s->catalog.size()));
    return true;
  }
  s->catalog.clear();
  std::string catalogUrl;
  std::vector<ReleaseCatalogAsset> releaseAssets;
  {
    CatalogReleaseStream release(releaseAssets, catalogUrl);
    esp_task_wdt_reset();
    if (!HttpDownloader::fetchUrl(kLatestReleaseApi, release)) {
      LOG_ERR("APPSTORE", "Failed to fetch latest GitHub release");
      return false;
    }
    esp_task_wdt_reset();
    if (!release.finish()) {
      LOG_ERR("APPSTORE", "Latest release asset stream was incomplete or invalid");
      return false;
    }
  }  // Drop release JSON parser scratch and sidecar inventory before next TLS handshake.

  // HTTP metadata is produced by a short-lived worker task. Give FreeRTOS idle
  // one scheduling point to reclaim a just-deleted worker stack before mbedTLS
  // allocates the next connection's record buffers.
  delay(1);
  LOG_INF("APPSTORE", "Found %u application ELF/JSON pairs in latest release",
          static_cast<unsigned>(releaseAssets.size()));

  if (!catalogUrl.empty()) {
    if (loadAggregateCatalog(releaseAssets, s->catalog, catalogUrl)) {
      LOG_INF("APPSTORE", "Loaded %u apps from aggregate release catalog",
              static_cast<unsigned>(s->catalog.size()));
      return true;
    }
    // A present aggregate catalog is the canonical metadata path for current
    // releases. If that one request fails, starting dozens of fresh TLS
    // handshakes cannot repair a low-memory/network failure and can make heap
    // fragmentation worse. Return cleanly; the user can retry the refresh.
    LOG_ERR("APPSTORE", "Aggregate catalog present but unavailable; refusing per-app TLS fan-out");
    std::vector<CatalogAsset>().swap(s->catalog);
    return false;
  }

  LOG_ERR("APPSTORE", "Release is missing %s; loading up to %u paired app manifests",
          kAggregateAppCatalogName, static_cast<unsigned>(releaseAssets.size()));
  const bool loaded = loadCatalogManifests(releaseAssets, s->catalog);
  LOG_INF("APPSTORE", "Legacy fallback loaded %u apps", static_cast<unsigned>(s->catalog.size()));
  return loaded;
}

uint32_t appCatalogCount() {
  auto* s = current();
  return s ? static_cast<uint32_t>(s->catalog.size()) : 0u;
}

bool appCatalogGet(uint32_t index, t5_app_release_asset_t* out) {
  auto* s = current();
  if (!s || !out || index >= s->catalog.size()) return false;
  *out = {};
  const auto& asset = s->catalog[index];
  std::strncpy(out->name, asset.name.c_str(), sizeof(out->name) - 1);
  out->name[sizeof(out->name) - 1] = '\0';
  out->size = asset.size;
  return true;
}

bool appCatalogManifestGet(uint32_t index, t5_app_manifest_t* out) {
  auto* s = current();
  if (!s || !out || index >= s->catalog.size() || !s->catalog[index].manifestValid) return false;
  *out = s->catalog[index].manifest;
  return true;
}

bool appCatalogVersionGet(uint32_t index, char* out, size_t capacity) {
  auto* s = current();
  if (!s || index >= s->catalog.size() || !s->catalog[index].manifestValid) return false;
  return copyVersion(s->catalog[index].version, out, capacity);
}

namespace {
constexpr RuntimePackages::PackageRuntimePolicy kCanonicalAppPolicy{
    "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};

bool verifiedManagedApp(const char* id, RuntimePackages::Identity& identity,
                        t5_app_manifest_t* manifest = nullptr) {
  identity = {};
  if (!id || !RuntimePackages::safeId(id)) return false;
  const std::string root = std::string("/Apps/") + id;
  if (!RuntimePackages::inspectInstalledOrdinarySdDirectory(root.c_str(), kCanonicalAppPolicy,
          RuntimePackages::installedCapabilityVersion, identity) ||
      identity.kind != RuntimePackages::Kind::Application ||
      std::strcmp(identity.id, id) || !t5_safe_elf_name(identity.artifact))
    return false;
  const std::string elf = root + "/" + identity.artifact;
  const std::string json = elf.substr(0, elf.size() - 4) + ".json";
  if (!Storage.exists(elf.c_str()) || !Storage.exists(json.c_str())) return false;
  t5_app_manifest_t parsed{};
  if (!readAppManifest(json.c_str(), parsed) ||
      std::strcmp(parsed.file_name, identity.artifact)) return false;
  if (manifest) *manifest = parsed;
  return true;
}
} // namespace

bool installedAppVersionGet(const char* fileName, char* out, size_t capacity) {
  auto* s = current();
  if (!s || !Storage.ready() || !t5_safe_elf_name(fileName) || !out || !capacity)
    return false;
  const std::string id(fileName, std::strlen(fileName) - 4);
  RuntimePackages::Identity canonical{};
  if (verifiedManagedApp(id.c_str(), canonical) &&
      std::strcmp(canonical.artifact, fileName) == 0)
    return copyVersion(canonical.version, out, capacity);
  const std::string destination = std::string("/Apps/") + fileName;
  const std::string sidecar = destination.substr(0, destination.size() - 4) + ".json";
  if (!RuntimePackages::recoverAppPair(fileName) ||
      !Storage.exists(destination.c_str()) || !Storage.exists(sidecar.c_str()) ||
      !RuntimePackages::inspectInstalledAppPair(destination.c_str(), sidecar.c_str(), fileName))
    return false;
  t5_app_manifest_t manifest{};
  std::string version;
  if (!readAppManifest(sidecar.c_str(), manifest, &version, false) ||
      std::strcmp(manifest.file_name, fileName)) return false;
  return copyVersion(version, out, capacity);
}

bool appCatalogDownload(uint32_t index) {
  auto* s = current();
  if (!s || !Storage.ready() || index >= s->catalog.size()) return false;
  const CatalogAsset selected = s->catalog[index];
  if (!safeAssetName(selected.name) ||
      !RuntimePackages::safePackageEntryName(selected.name.c_str()) ||
      !selected.manifestValid ||
      (selected.manifestJson.empty() && selected.manifestUrl.empty()) ||
      selected.size < 52 || selected.size > 8u * 1024u * 1024u) return false;

  std::string json = selected.manifestJson, version;
  t5_app_manifest_t manifest{};
  if ((json.empty() && !HttpDownloader::fetchUrl(selected.manifestUrl, json)) ||
      json.empty() || json.size() > 4096 ||
      !parseAppManifest(json, manifest, &version, true) ||
      !manifest.compatible || selected.name != manifest.file_name ||
      version != selected.version) return false;
  JsonDocument metadata;
  if (deserializeJson(metadata, json) || !metadata.is<JsonObjectConst>() ||
      !metadata["sha256"].is<const char*>() ||
      !metadata["size_bytes"].is<unsigned>() ||
      metadata["size_bytes"].as<unsigned>() != selected.size) {
    LOG_ERR("APPSTORE", "Release metadata lacks matching executable length and digest");
    return false;
  }
  const char* digest = metadata["sha256"].as<const char*>();
  if (!RuntimePackages::validSha256Hex(digest)) return false;
  char installed[T5_APP_VERSION_MAX]{};
  if (installedAppVersionGet(selected.name.c_str(), installed, sizeof(installed)) &&
      RuntimePackages::comparePackageVersions(version.c_str(), installed) !=
          RuntimePackages::VersionOrder::Newer) return false;

  // There is exactly one publication mechanism for all four ordinary package
  // kinds. A release is first converted to an exclusive canonical SD source;
  // that source is independently verified by the shared transaction engine.
  return RuntimeOnlinePackages::installApplication(selected.name.c_str(),
      version.c_str(), selected.url.c_str(), json, selected.size, digest);
}

bool installedRefresh() {
  auto* s = current();
  if (!s) return false;
  s->installed.clear();
  if (!RuntimePackages::recoverAppInventory())
    LOG_ERR("APPSTORE", "Some legacy app updates require manual recovery");
  HalFile dir = Storage.open("/Apps", O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) return false;
  while (s->installed.size() < 128) {
    esp_task_wdt_reset();
    HalFile file = dir.openNextFile();
    if (!file.isOpen()) break;
    char name[128]{};
    file.getName(name, sizeof(name));
    const bool isDir = file.isDirectory();
    file.close();
    const std::string filename(name);
    if (isDir) {
      RuntimePackages::Identity identity{};
      t5_app_manifest_t manifest{};
      if (!verifiedManagedApp(name, identity, &manifest) ||
          !std::strcmp(manifest.file_name, "springboard.elf")) continue;
      s->installed.push_back(manifest);
      continue;
    }
    if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".json") continue;
    t5_app_manifest_t manifest{};
    if (!readAppManifest((std::string("/Apps/") + filename).c_str(), manifest)) continue;
    if (filename != std::string(manifest.file_name).substr(0, std::strlen(manifest.file_name) - 4) + ".json") continue;
    if (!std::strcmp(manifest.file_name, "springboard.elf")) continue;
    if (!Storage.exists((std::string("/Apps/") + manifest.file_name).c_str())) continue;
    if (Storage.exists((std::string("/Apps/") + manifest.file_name + ".bak").c_str()) ||
        Storage.exists((std::string("/Apps/") + filename + ".bak").c_str())) continue;
    s->installed.push_back(manifest);
  }
  dir.close();
  std::sort(s->installed.begin(), s->installed.end(), [](const t5_app_manifest_t& a, const t5_app_manifest_t& b) {
    return std::strcmp(a.display_name, b.display_name) < 0;
  });
  return true;
}

uint32_t installedCount() { auto* s = current(); return s ? s->installed.size() : 0; }
bool installedGet(uint32_t index, t5_app_manifest_t* out) {
  auto* s = current();
  if (!s || !out || index >= s->installed.size()) return false;
  *out = s->installed[index]; return true;
}
bool requestLaunch(uint32_t index) {
  auto* s = current();
  if (!s || s->exiting || index >= s->installed.size() || !s->installed[index].compatible)
    return false;
  const char* artifact = s->installed[index].file_name;
  const std::string id(artifact, std::strlen(artifact) - 4);
  RuntimePackages::Identity managed{};
  if (verifiedManagedApp(id.c_str(), managed) &&
      !std::strcmp(managed.artifact, artifact))
    s->launchPath = std::string("/sd/Apps/") + id + "/" + artifact;
  else
    s->launchPath = std::string("/sd/Apps/") + artifact;
  s->exiting = true;
  return true;
}

bool drawIcon(int32_t x, int32_t y, const char* icon, uint8_t size, bool black) {
  auto* s = current(); return s && FontAwesomeIcons::draw(s->renderer, x, y, icon, size, black);
}
void drawLabel(int32_t x, int32_t y, int32_t w, const char* value) {
  auto* s = current();
  if (!s || !value || w <= 0) return;
  const auto label = s->renderer.truncatedText(UI_12_FONT_ID, value, w);
  const int textWidth = s->renderer.getTextWidth(UI_12_FONT_ID, label.c_str());
  s->renderer.drawText(UI_12_FONT_ID, x + (w - textWidth) / 2, y, label.c_str());
}

const t5_app_api_v1 api = {T5_APP_ABI_VERSION,
                           sizeof(t5_app_api_v1),
                           width,
                           height,
                           clear,
                           text,
                           rect,
                           present,
                           poll,
                           clockMs,
                           dirOpen,
                           dirNext,
                           dirClose,
                           appCatalogRefresh,
                           appCatalogCount,
                           appCatalogGet,
                           appCatalogDownload,
                           setBackExitsApp,
                           nativeSettingsCategoryCount,
                           nativeSettingsCategoryGet,
                           nativeSettingsCount,
                           nativeSettingsGet,
                           nativeSettingsActivate,
                           nativeSettingsRender,
                           nativeSettingsTouch,
                           installedRefresh, installedCount, installedGet, requestLaunch, drawIcon, drawLabel,
                           appCatalogManifestGet,
                           installedAppVersionGet,
                           appCatalogVersionGet,
                           presentServiced};
}  // namespace

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  return version == T5_APP_ABI_VERSION && current() ? &api : nullptr;
}

esp_err_t runNativeApp(const char* path, GfxRenderer& renderer, MappedInputManager& input) {
  lastLaunchError.clear();
  if (session) {
    lastLaunchError = "Another native application is already running.";
    return ESP_ERR_INVALID_STATE;
  }
  queuedLaunch.clear();
  homeRequested = false;
  firmwareActionPending = false;
  // Legacy loose ELFs still work in Browse Files. Present sidecars are enforced.
  if (!path || std::strncmp(path, "/sd/", 4)) {
    lastLaunchError = "Invalid native application path.";
    return ESP_ERR_INVALID_ARG;
  }
  const std::string elf(path + 3);
  if (elf.size() < 4) {
    lastLaunchError = "Invalid native application filename.";
    return ESP_ERR_INVALID_ARG;
  }
  const std::string filename = elf.substr(elf.find_last_of('/') + 1);
  const std::string sidecar = elf.substr(0, elf.size() - 4) + ".json";
  // Nested /Apps/<id>/<artifact> entries are independently verified against
  // their exact canonical package inventory. A failed verification never
  // falls through to the loose-file compatibility loader.
  const size_t nestedSlash = elf.compare(0, 6, "/Apps/") == 0 ?
      elf.find('/', 6) : std::string::npos;
  std::string canonicalRoot;
  if (nestedSlash != std::string::npos) {
    const std::string id = elf.substr(6, nestedSlash - 6);
    RuntimePackages::Identity identity{};
    if (!verifiedManagedApp(id.c_str(), identity) ||
        std::strcmp(identity.artifact, filename.c_str())) {
      lastLaunchError = "Canonical application package or executable is invalid.";
      return ESP_ERR_NOT_SUPPORTED;
    }
    canonicalRoot = elf.substr(0, nestedSlash);
  }
  // Managed /Apps updates recover before any sidecar/ELF can be loaded.
  if (elf.compare(0, 6, "/Apps/") == 0 &&
      nestedSlash == std::string::npos && t5_safe_elf_name(filename.c_str()) &&
      !RuntimePackages::recoverAppPair(filename.c_str())) {
    lastLaunchError = "Application update cannot be safely recovered.";
    return ESP_ERR_INVALID_STATE;
  }
  if (Storage.exists((elf + ".bak").c_str()) || Storage.exists((sidecar + ".bak").c_str())) {
    lastLaunchError = "Application update is incomplete; backup files remain.";
    return ESP_ERR_INVALID_STATE;
  }
  if (Storage.exists(sidecar.c_str())) {
    t5_app_manifest_t manifest{};
    if (!readAppManifest(sidecar.c_str(), manifest)) {
      lastLaunchError = "Application manifest is invalid.";
      return ESP_ERR_NOT_SUPPORTED;
    }
    if (filename != manifest.file_name) {
      lastLaunchError = "Manifest file name does not match the ELF.";
      return ESP_ERR_NOT_SUPPORTED;
    }
    if (!RuntimePackages::inspectInstalledAppPair(elf.c_str(), sidecar.c_str(), filename.c_str())) {
      lastLaunchError = "Application metadata or executable is unavailable.";
      return ESP_ERR_NOT_SUPPORTED;
    }
    if (!manifest.compatible) {
      lastLaunchError = std::string("Requires firmware ") + manifest.min_firmware_version +
                        "; running " + CROSSPOINT_COMPAT_VERSION + ".";
      return ESP_ERR_NOT_SUPPORTED;
    }
  }
  // Refuse a concurrent managed-package replacement before changing app UI
// state. Keep failed dlclose generations pinned for safe recovery.
if (!canonicalRoot.empty() &&
    !RuntimePackages::systemPackageUseGate().pin(canonicalRoot.c_str())) {
  lastLaunchError = "Managed application is being replaced or is unavailable.";
  return ESP_ERR_INVALID_STATE;
}
HalPowerManager::Lock powerLock;
  RenderLock lock;
  const auto orientation = renderer.getOrientation();
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  nativeNavigationBoundary();
  input.clearInjectedButtonTap();
  input.update();
  Session active{renderer, input, xTaskGetCurrentTaskHandle()};
  session = &active;
  nativeSettingsBegin(renderer, input);
  nativeSystemUiBegin();
  esp_task_wdt_reset();
  // Pin the exact installed generation before mapping and release only after
  // the ELF has returned and dlclose has succeeded. Failed unloads retain the
  // pin; replacement and uninstall must not race executable memory.
  nativeStreamsBegin();
  const esp_err_t result = launch_elf_app(path);
  nativeStreamsEnd();
  if (!canonicalRoot.empty() && result == ESP_OK)
    (void)RuntimePackages::systemPackageUseGate().unpin(canonicalRoot.c_str());
  // Clean up even when an app returns without calling its GPS stop callback.
  GpsDriverRuntime::stop();
  if (result != ESP_OK && lastLaunchError.empty()) {
    char message[80];
    std::snprintf(message, sizeof(message), "ELF loader failed with error 0x%lX.",
                  static_cast<unsigned long>(result));
    lastLaunchError = message;
  }
  const auto systemNavigation = nativeSystemUiTakeNavigation();
  homeRequested = homeRequested || systemNavigation == NativeSystemUiNavigation::Home;
  queuedLaunch = active.launchPath;
  if (active.directory.isOpen()) active.directory.close();
  active.catalog.clear();
  session = nullptr;
  nativeNavigationBoundary();
  nativeNavigationRetry(); // An installer may have added the navigation provider.
  nativeSettingsEnd();
  renderer.setOrientation(orientation);
  renderer.setRenderMode(mode);
  renderer.requestNextRefresh(HalDisplay::FULL_REFRESH);
  unsigned long quiet = millis();
  do {
    input.update();
    if (input.wasAnyPressed() || input.wasAnyReleased() || input.wasTouchHomeButtonPressed() ||
        input.isPressed(MappedInputManager::Button::Back) || input.isPressed(MappedInputManager::Button::Power)) {
      quiet = millis();
    }
    esp_task_wdt_reset();
    delay(10);
  } while (millis() - quiet < 350);
  input.clearInjectedButtonTap();
  firmwareActionPending = nativeSettingsDispatchPendingAction(renderer, input, path);
  firmwareActionPending = firmwareActionPending || systemNavigation == NativeSystemUiNavigation::Keyboard;
  returned = true;
  return result;
}

bool consumeNativeAppReturn() { const bool value = returned; returned = false; return value; }

bool runNativeSpringboard(GfxRenderer& renderer, MappedInputManager& input, bool resume) {
  if (resume && homeRequested) return false;
  const char* springboard = "/sd/Apps/springboard.elf";
  auto showError = [&](const char* message) {
    RenderLock lock;
    renderer.clearScreen();
    renderer.drawText(UI_12_FONT_ID, 24, 40, "Apps");
    const std::string msg(message);
    const auto split = msg.find(' ', msg.size() / 2);
    renderer.drawText(UI_12_FONT_ID, 24, 100, msg.substr(0, split).c_str());
    if (split != std::string::npos) renderer.drawText(UI_12_FONT_ID, 24, 136, msg.substr(split + 1).c_str());
    renderer.drawText(UI_12_FONT_ID, 24, 200, "Tap or press Back to return.");
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    for (;;) {
      esp_task_wdt_reset(); delay(20); input.update();
      MappedInputManager::TouchPoint point{};
      if (input.wasTouchTapped(point, renderer) || input.wasAnyPressed() || input.wasTouchHomeButtonPressed()) break;
    }
  };
  // Recovery must run while no ELF is mapped, before checking for springboard
  // files: a power cut can leave only springboard.elf.bak at this point.
  if (Storage.exists("/Apps")) {
    if (!RuntimePackages::recoverAppInventory())
      LOG_ERR("APPSTORE", "Some managed apps require manual recovery");
    if (!RuntimePackages::recoverAppPair("springboard.elf")) {
      showError("Springboard update cannot be safely recovered.");
      return false;
    }
  }
  if (!Storage.exists("/Apps/springboard.elf") || !Storage.exists("/Apps/springboard.json")) {
    showError("Copy springboard.elf and .json to /Apps.");
    return false;
  }
  for (;;) {
    const auto result = runNativeApp(springboard, renderer, input);
    if (result != ESP_OK) {
      showError(lastLaunchError.empty() ? "Apps launcher failed." : lastLaunchError.c_str());
      return false;
    }
    if (firmwareActionPending) return true;
    if (homeRequested || queuedLaunch.empty()) return false;
    const std::string selected = queuedLaunch;
    const std::string selectedName = selected.substr(selected.find_last_of('/') + 1);
    if (t5_safe_elf_name(selectedName.c_str()) &&
        !RuntimePackages::recoverAppPair(selectedName.c_str())) {
      showError("Application update cannot be safely recovered.");
      continue;
    }
    const std::string sidecar = selected.substr(3, selected.size() - 7) + ".json";
    if (!Storage.exists(sidecar.c_str())) { showError("Application manifest is missing."); continue; }
    const auto appResult = runNativeApp(selected.c_str(), renderer, input);
    if (firmwareActionPending) return true;
    if (homeRequested) return false;
    if (appResult != ESP_OK) {
      showError(lastLaunchError.empty() ? "Application launch failed." : lastLaunchError.c_str());
    }
  }
}
