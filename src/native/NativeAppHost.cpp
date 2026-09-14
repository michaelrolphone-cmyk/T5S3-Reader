#include "NativeAppHost.h"
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
  uint64_t size = 0;
};

struct Session {
  GfxRenderer& renderer;
  MappedInputManager& input;
  TaskHandle_t owner;
  HalFile directory;
  std::vector<CatalogAsset> catalog;
  bool exiting = false;
};
Session* session = nullptr;
bool returned = false;
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
  if (s->input.isPressed(Button::Back) || s->input.isPressed(Button::Power) || s->input.wasTouchHomeButtonPressed()) {
    s->exiting = true;
  }
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
  size_t assets = json.find("\"assets\"");
  if (assets == std::string::npos) return false;
  size_t p = json.find('[', assets);
  if (p == std::string::npos) return false;
  ++p;

  while (p < json.size() && catalog.size() < kMaxCatalogAssets) {
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
    if (jsonStringField(object, "name", asset.name) && safeAssetName(asset.name) &&
        jsonStringField(object, "browser_download_url", asset.url)) {
      asset.size = jsonUintField(object, "size");
      catalog.push_back(std::move(asset));
    }
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

  char destination[sizeof("/Apps/") + T5_APP_ASSET_NAME_MAX];
  const int written = std::snprintf(destination, sizeof(destination), "/Apps/%s", asset.name.c_str());
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(destination)) return false;

  esp_task_wdt_reset();
  const auto result = HttpDownloader::downloadToFile(asset.url, destination, [](size_t, size_t) {
    esp_task_wdt_reset();
  });
  esp_task_wdt_reset();
  return result == HttpDownloader::OK;
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
                           appCatalogDownload};
}  // namespace

extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t version) {
  return version == T5_APP_ABI_VERSION && current() ? &api : nullptr;
}

esp_err_t runNativeApp(const char* path, GfxRenderer& renderer, MappedInputManager& input) {
  // Render task must not paint the browser over native app output.
  if (session) return ESP_ERR_INVALID_STATE;
  HalPowerManager::Lock powerLock;
  RenderLock lock;
  const auto orientation = renderer.getOrientation();
  const auto mode = renderer.getRenderMode();
  renderer.setRenderMode(GfxRenderer::BW);
  input.clearInjectedButtonTap();
  input.update();
  Session active{renderer, input, xTaskGetCurrentTaskHandle()};
  session = &active;
  esp_task_wdt_reset();
  const esp_err_t result = launch_elf_app(path);
  if (active.directory.isOpen()) active.directory.close();
  active.catalog.clear();
  session = nullptr;
  renderer.setOrientation(orientation);
  renderer.setRenderMode(mode);
  renderer.requestNextRefresh(HalDisplay::FULL_REFRESH);
  // Consume the exit gesture before returning control to the browser.
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
  returned = true;
  return result;
}

bool consumeNativeAppReturn() { const bool value = returned; returned = false; return value; }
