#include "AppCatalogIndex.h"

#include <ArduinoJson.h>
#include <Logging.h>
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
  if (url.empty()) {
    LOG_ERR("APPSTORE", "Aggregate catalog URL is empty");
    return false;
  }

  std::string json;
  esp_task_wdt_reset();
  if (!HttpDownloader::fetchUrl(url, json)) {
    LOG_ERR("APPSTORE", "Aggregate catalog download failed: %s", url.c_str());
    return false;
  }
  if (json.empty() || json.size() > kMaxCatalogBytes) {
    LOG_ERR("APPSTORE", "Aggregate catalog size invalid: %u bytes (limit %u)",
            static_cast<unsigned>(json.size()), static_cast<unsigned>(kMaxCatalogBytes));
    return false;
  }

  JsonDocument doc;
  const auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("APPSTORE", "Aggregate catalog JSON parse failed: %s", error.c_str());
    return false;
  }
  if (!doc.is<JsonObjectConst>() || doc["schema"] != 1 || !doc["apps"].is<JsonArrayConst>()) {
    LOG_ERR("APPSTORE", "Aggregate catalog requires schema=1 and an apps array");
    return false;
  }

  const JsonArrayConst apps = doc["apps"].as<JsonArrayConst>();
  if (apps.size() == 0 || apps.size() > kMaxCatalogEntries) {
    LOG_ERR("APPSTORE", "Aggregate catalog app count invalid: %u (limit %u)",
            static_cast<unsigned>(apps.size()), static_cast<unsigned>(kMaxCatalogEntries));
    return false;
  }

  manifests.reserve(apps.size());
  for (JsonVariantConst entry : apps) {
    esp_task_wdt_reset();
    if (!entry.is<JsonObjectConst>()) {
      LOG_ERR("APPSTORE", "Aggregate catalog entry %u is not an object",
              static_cast<unsigned>(manifests.size() + 1));
      return false;
    }
    std::string manifest;
    serializeJson(entry, manifest);
    if (manifest.empty() || manifest.size() > kMaxManifestBytes) {
      LOG_ERR("APPSTORE", "Aggregate catalog entry %u exceeds manifest size limit",
              static_cast<unsigned>(manifests.size() + 1));
      return false;
    }
    manifests.push_back(std::move(manifest));
  }
  return manifests.size() == apps.size();
}
