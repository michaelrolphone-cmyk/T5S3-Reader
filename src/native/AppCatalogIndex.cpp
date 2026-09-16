#include "AppCatalogIndex.h"

#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <utility>

#include "network/HttpDownloader.h"

namespace {
constexpr size_t kMaxCatalogBytes = 64 * 1024;
constexpr size_t kMaxCatalogEntries = 128;
constexpr size_t kMaxManifestBytes = 2048;
}

bool fetchAppCatalogIndex(const std::string& url, std::vector<std::string>& manifests) {
  manifests.clear();
  if (url.empty()) return false;

  std::string json;
  esp_task_wdt_reset();
  if (!HttpDownloader::fetchUrl(url, json) || json.empty() || json.size() > kMaxCatalogBytes) return false;

  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc.is<JsonObjectConst>() || doc["schema"] != 1 ||
      !doc["apps"].is<JsonArrayConst>()) {
    return false;
  }

  const JsonArrayConst apps = doc["apps"].as<JsonArrayConst>();
  if (apps.size() == 0 || apps.size() > kMaxCatalogEntries) return false;

  manifests.reserve(apps.size());
  for (JsonVariantConst entry : apps) {
    esp_task_wdt_reset();
    if (!entry.is<JsonObjectConst>()) return false;
    std::string manifest;
    serializeJson(entry, manifest);
    if (manifest.empty() || manifest.size() > kMaxManifestBytes) return false;
    manifests.push_back(std::move(manifest));
  }
  return manifests.size() == apps.size();
}
