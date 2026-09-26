#include "AppCatalogIndex.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <esp_task_wdt.h>
#include <utility>

#include "network/HttpDownloader.h"
#include "runtime/memory/PsramJson.h"

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

  RuntimeMemory::PsramTextStream json(kMaxCatalogBytes);
  esp_task_wdt_reset();
  const bool fetched = json.good() && HttpDownloader::fetchUrl(url, json);
  // The native HTTP adapter completes on a short-lived worker. Yield once on
  // both success and failure so the idle task can reclaim that worker stack
  // before ArduinoJson allocation or a fallback TLS connection starts.
  delay(1);
  if (!fetched || !json.good()) {
    LOG_ERR("APPSTORE", "Aggregate catalog download failed: %s", url.c_str());
    return false;
  }
  if (json.empty()) {
    LOG_ERR("APPSTORE", "Aggregate catalog is empty");
    return false;
  }

  RuntimeMemory::PsramJsonAllocator allocator;
  JsonDocument doc(&allocator);
  const auto error = deserializeJson(doc, json.chars(), json.size());
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
