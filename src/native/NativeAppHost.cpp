#include "NativeAppHost.h"
#include "AppManifest.h"
#include <AppManifestRules.h>
#include "components/FontAwesomeIcons.h"
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <NativeAppLauncher.h>
#include <T5AppApi.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <algorithm>
#include <cctype>
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

namespace {
constexpr const char* kLatestReleaseApi =
    "https://api.github.com/repos/michaelrolphone-cmyk/T5S3-Reader/releases/latest";
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr size_t kMaxCatalogAssets = 64;

struct CatalogAsset {
  std::string name;
  std::string url;
  std::string manifestUrl;
  uint64_t size = 0;
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
};
Session* session = nullptr;
bool returned = false;
std::string queuedLaunch;
bool homeRequested = false;
bool firmwareActionPending = false;
Session* current() { return session && session->owner == xTaskGetCurrentTaskHandle() ? session : nullptr; }
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

bool endsWithElf(const std::string& name) {
  if (name.size() < 4) return false;
  const size_t n = name.size();
  return name[n - 4] == '.' && std::tolower(static_cast<unsigned char>(name[n - 3])) == 'e' &&
         std::tolower(static_cast<unsigned char>(name[n - 2])) == 'l' &&
         std::tolower(static_cast<unsigned char>(name[n - 1])) == 'f';
}

bool safeAssetName(const std::string& name) {
  if (name.empty() || name.size() >= T5_APP_ASSET_NAME_MAX || !endsWithElf(name)) return false;
  if (name.find("..") != std::string::npos) return false;
  return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
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

bool parseCatalog(const std::string& json, std::vector<CatalogAsset>& catalog) {
  catalog.clear();
  std::vector<CatalogAsset> manifests;
  size_t assets = json.find("\"assets\"");
  if (assets == std::string::npos) return false;
  size_t p = json.find('[', assets);
  if (p == std::string::npos) return false;
  ++p;

  while (p < json.size()) {
    p = json.find_first_not_of(" \t\r\n,", p);
    if (p == std::string::npos || json[p] == ']') break;
    p = json.find('{', p);
    if (p == std::string::npos) break;

    const size_t start = p;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    size_t end = std::string::npos;
    for (; p < json.size(); ++p) {
      const char c = json[p];
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == '"') {
          inString = false;
        }
        continue;
      }
      if (c == '"') {
        inString = true;
      } else if (c == '{') {
        ++depth;
      } else if (c == '}') {
        --depth;
        if (depth == 0) {
          end = p;
          ++p;
          break;
        }
      }
    }
    if (end == std::string::npos) return false;

    const std::string object = json.substr(start, end - start + 1);
    CatalogAsset asset;
    if (jsonStringField(object, "name", asset.name) &&
        jsonStringField(object, "browser_download_url", asset.url)) {
      asset.size = jsonUintField(object, "size");
      if (safeAssetName(asset.name) && catalog.size() < kMaxCatalogAssets) catalog.push_back(std::move(asset));
      else if (asset.name.size() > 5 && asset.name.substr(asset.name.size() - 5) == ".json" &&
               manifests.size() < kMaxCatalogAssets) manifests.push_back(std::move(asset));
    }
  }
  for (auto& asset : catalog) {
    const auto expected = asset.name.substr(0, asset.name.size() - 4) + ".json";
    for (const auto& manifest : manifests) if (manifest.name == expected) asset.manifestUrl = manifest.url;
  }
  return true;
}

bool connectSavedWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  WIFI_STORE.loadFromFile();
  const WifiCredential* cred = nullptr;
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) cred = WIFI_STORE.findCredential(last);
  if (!cred) {
    const auto& credentials = WIFI_STORE.getCredentials();
    if (!credentials.empty()) cred = &credentials.front();
  }
  if (!cred || cred->ssid.empty()) return false;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  delay(100);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String hostname = "CrossPoint-Reader-" + mac;
  WiFi.setHostname(hostname.c_str());

  if (cred->password.empty()) {
    WiFi.begin(cred->ssid.c_str());
  } else {
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  }

  const uint32_t started = millis();
  while (millis() - started < kWifiConnectTimeoutMs) {
    esp_task_wdt_reset();
    if (WiFi.status() == WL_CONNECTED) {
      WIFI_STORE.setLastConnectedSsid(cred->ssid);
      return true;
    }
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool appCatalogRefresh() {
  auto* s = current();
  if (!s) return false;
  s->catalog.clear();
  if (!connectSavedWifi()) return false;

  std::string json;
  esp_task_wdt_reset();
  if (!HttpDownloader::fetchUrl(kLatestReleaseApi, json)) return false;
  esp_task_wdt_reset();
  return parseCatalog(json, s->catalog);
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

bool appCatalogDownload(uint32_t index) {
  auto* s = current();
  if (!s || !Storage.ready() || index >= s->catalog.size()) return false;
  const auto& asset = s->catalog[index];
  if (!safeAssetName(asset.name)) return false;

  if (asset.manifestUrl.empty()) return false;
  std::string json;
  t5_app_manifest_t manifest{};
  if (!HttpDownloader::fetchUrl(asset.manifestUrl, json) || !parseAppManifest(json, manifest) ||
      !manifest.compatible || asset.name != manifest.file_name) return false;
  if (!Storage.mkdir("/Apps") && !Storage.exists("/Apps")) return false;
  const std::string destination = std::string("/Apps/") + asset.name;
  const std::string sidecar = destination.substr(0, destination.size() - 4) + ".json";
  const std::string temporary = destination + ".part";
  const std::string stagedJson = sidecar + ".part";
  const std::string backup = destination + ".bak";
  const std::string backupJson = sidecar + ".bak";
  // A prior interrupted transaction is recovered before making another install.
  if (Storage.exists(backup.c_str()) || Storage.exists(backupJson.c_str())) {
    if (Storage.exists(backup.c_str())) {
      Storage.remove(destination.c_str());
      if (!Storage.rename(backup.c_str(), destination.c_str())) return false;
    }
    if (Storage.exists(backupJson.c_str())) {
      Storage.remove(sidecar.c_str());
      if (!Storage.rename(backupJson.c_str(), sidecar.c_str())) return false;
    }
  }
  Storage.remove(temporary.c_str());
  Storage.remove(stagedJson.c_str());
  if (!Storage.writeFile(stagedJson.c_str(), String(json.c_str()))) return false;
  const auto result = HttpDownloader::downloadToFile(asset.url, temporary, [](size_t, size_t) { esp_task_wdt_reset(); });
  HalFile staged = Storage.open(temporary.c_str(), O_RDONLY);
  const bool sizeOk = staged.isOpen() && (asset.size == 0 || staged.fileSize64() == asset.size);
  staged.close();
  if (result != HttpDownloader::OK || !sizeOk) {
    Storage.remove(temporary.c_str()); Storage.remove(stagedJson.c_str()); return false;
  }
  const bool hadElf = Storage.exists(destination.c_str());
  const bool hadJson = Storage.exists(sidecar.c_str());
  if (hadElf && !Storage.rename(destination.c_str(), backup.c_str())) return false;
  if (hadJson && !Storage.rename(sidecar.c_str(), backupJson.c_str())) {
    if (hadElf) Storage.rename(backup.c_str(), destination.c_str());
    return false;
  }
  if (!Storage.rename(temporary.c_str(), destination.c_str()) || !Storage.rename(stagedJson.c_str(), sidecar.c_str())) {
    Storage.remove(destination.c_str()); Storage.remove(sidecar.c_str());
    if (hadElf) Storage.rename(backup.c_str(), destination.c_str());
    if (hadJson) Storage.rename(backupJson.c_str(), sidecar.c_str());
    return false;
  }
  Storage.remove(backup.c_str()); Storage.remove(backupJson.c_str());
  return true;
}

bool installedRefresh() {
  auto* s = current();
  if (!s) return false;
  s->installed.clear();
  HalFile dir = Storage.open("/Apps", O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) return false;
  while (s->installed.size() < 128) {
    esp_task_wdt_reset();
    HalFile file = dir.openNextFile();
    if (!file.isOpen()) break;
    char name[128] = {};
    file.getName(name, sizeof(name));
    const bool isDir = file.isDirectory();
    file.close();
    const std::string filename(name);
    if (isDir || filename.size() < 6 || filename.substr(filename.size() - 5) != ".json") continue;
    t5_app_manifest_t manifest{};
    if (!readAppManifest((std::string("/Apps/") + filename).c_str(), manifest)) continue;
    if (filename != std::string(manifest.file_name).substr(0, std::strlen(manifest.file_name) - 4) + ".json") continue;
    if (!std::strcmp(manifest.file_name, "springboard.elf")) continue;
    if (!Storage.exists((std::string("/Apps/") + manifest.file_name).c_str())) continue;
    // Incomplete update pairs are never launched.
    if (Storage.exists((std::string("/Apps/") + manifest.file_name + ".bak").c_str()) ||
        Storage.exists((std::string("/Apps/") + filename + ".bak").c_str())) continue;
    s->installed.push_back(manifest);
  }
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
  if (!s || s->exiting || index >= s->installed.size() || !s->installed[index].compatible) return false;
  s->launchPath = std::string("/sd/Apps/") + s->installed[index].file_name;
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
                           installedRefresh, installedCount, installedGet, requestLaunch, drawIcon, drawLabel};
}  // namespace

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  return version == T5_APP_ABI_VERSION && current() ? &api : nullptr;
}

esp_err_t runNativeApp(const char* path, GfxRenderer& renderer, MappedInputManager& input) {
  if (session) return ESP_ERR_INVALID_STATE;
  queuedLaunch.clear();
  homeRequested = false;
  firmwareActionPending = false;
  // Legacy loose ELFs still work in Browse Files. Present sidecars are enforced.
  if (!path || std::strncmp(path, "/sd/", 4)) return ESP_ERR_INVALID_ARG;
  const std::string elf(path + 3);
  if (elf.size() < 4) return ESP_ERR_INVALID_ARG;
  const std::string sidecar = elf.substr(0, elf.size() - 4) + ".json";
  if (Storage.exists((elf + ".bak").c_str()) || Storage.exists((sidecar + ".bak").c_str())) return ESP_ERR_INVALID_STATE;
  if (Storage.exists(sidecar.c_str())) {
    t5_app_manifest_t manifest{};
    if (!readAppManifest(sidecar.c_str(), manifest) || !manifest.compatible ||
        elf.substr(elf.find_last_of('/') + 1) != manifest.file_name) return ESP_ERR_NOT_SUPPORTED;
  }
  HalPowerManager::Lock powerLock;
  RenderLock lock;
  const auto orientation = renderer.getOrientation();
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  input.clearInjectedButtonTap();
  input.update();
  Session active{renderer, input, xTaskGetCurrentTaskHandle()};
  session = &active;
  nativeSettingsBegin(renderer, input);
  nativeSystemUiBegin();
  esp_task_wdt_reset();
  const esp_err_t result = launch_elf_app(path);
  const auto systemNavigation = nativeSystemUiTakeNavigation();
  homeRequested = homeRequested || systemNavigation == NativeSystemUiNavigation::Home;
  queuedLaunch = active.launchPath;
  if (active.directory.isOpen()) active.directory.close();
  active.catalog.clear();
  session = nullptr;
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
  if (!Storage.exists("/Apps/springboard.elf") || !Storage.exists("/Apps/springboard.json")) {
    showError("Copy springboard.elf and .json to /Apps.");
    return false;
  }
  for (;;) {
    const auto result = runNativeApp(springboard, renderer, input);
    if (result != ESP_OK) { showError("Apps launcher failed; check firmware version."); return false; }
    if (firmwareActionPending) return true;
    if (homeRequested || queuedLaunch.empty()) return false;
    const std::string selected = queuedLaunch;
    const std::string sidecar = selected.substr(3, selected.size() - 7) + ".json";
    if (!Storage.exists(sidecar.c_str())) { showError("Application manifest is missing."); continue; }
    const auto appResult = runNativeApp(selected.c_str(), renderer, input);
    if (firmwareActionPending) return true;
    if (homeRequested) return false;
    if (appResult != ESP_OK) showError("Application failed or needs newer firmware.");
  }
}
