#include <T5AppApi.h>
#include <T5FontApi.h>

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_rom_crc.h>

#include <cstring>
#include <string>
#include <vector>

#include "FontCatalogConfig.h"
#include "FontInstaller.h"
#include "SdCardFontGlobals.h"
#include "network/HttpDownloader.h"

namespace {

struct ManifestFile {
  std::string name;
  size_t size = 0;
  uint32_t crc32 = 0;
};

struct ManifestFamily {
  std::string name;
  std::string description;
  std::vector<ManifestFile> files;
  size_t totalSize = 0;
  bool installed = false;
  bool hasUpdate = false;
};

std::string baseUrl;
std::vector<ManifestFamily> families;

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

FontInstaller& installer() {
  static FontInstaller value(sdFontSystem.registry());
  return value;
}

bool computeCrc32(const char* path, uint32_t& out) {
  FsFile f;
  if (!Storage.openFileForRead("FONT", path, f)) return false;
  uint8_t buf[128];
  uint32_t crc = 0;
  while (f.available()) {
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    crc = esp_rom_crc32_le(crc, buf, static_cast<uint32_t>(n));
  }
  f.close();
  out = crc;
  return true;
}

t5_font_result_t refreshCatalog() {
  if (!active()) return T5_FONT_UNAVAILABLE;

  static constexpr const char* tempPath = "/fonts_manifest.tmp";
  const auto download = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, tempPath, nullptr);
  if (download != HttpDownloader::OK) {
    Storage.remove(tempPath);
    return T5_FONT_NETWORK_ERROR;
  }

  FsFile file;
  if (!Storage.openFileForRead("FONT", tempPath, file)) {
    Storage.remove(tempPath);
    return T5_FONT_STORAGE_ERROR;
  }

  JsonDocument doc;
  const auto err = deserializeJson(doc, file);
  file.close();
  Storage.remove(tempPath);
  if (err || (doc["version"] | 0) != FONTS_MANIFEST_VERSION) return T5_FONT_MANIFEST_ERROR;

  baseUrl = doc["baseUrl"] | "";
  families.clear();
  installer().refreshRegistry();

  for (JsonObject fObj : doc["families"].as<JsonArray>()) {
    ManifestFamily family;
    family.name = fObj["name"] | "";
    family.description = fObj["description"] | "";
    if (!FontInstaller::isValidFamilyName(family.name.c_str())) return T5_FONT_MANIFEST_ERROR;

    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      ManifestFile entry;
      entry.name = fileObj["name"] | "";
      entry.size = fileObj["size"] | 0;
      if (!FontInstaller::isValidCpfontFilename(entry.name.c_str()) || !fileObj["crc32"].is<uint32_t>()) {
        return T5_FONT_MANIFEST_ERROR;
      }
      entry.crc32 = fileObj["crc32"].as<uint32_t>();
      family.totalSize += entry.size;
      family.files.push_back(std::move(entry));
    }

    family.installed = installer().isFamilyInstalled(family.name.c_str());
    if (family.installed) {
      for (const auto& entry : family.files) {
        char path[128];
        FontInstaller::buildFontPath(family.name.c_str(), entry.name.c_str(), path, sizeof(path));
        FsFile installedFile;
        if (!Storage.openFileForRead("FONT", path, installedFile)) {
          family.hasUpdate = true;
          break;
        }
        const size_t actual = installedFile.fileSize();
        installedFile.close();
        if (actual != entry.size) {
          family.hasUpdate = true;
          break;
        }
      }
    }
    families.push_back(std::move(family));
  }

  return T5_FONT_OK;
}

uint32_t familyCount() { return active() ? static_cast<uint32_t>(families.size()) : 0; }

bool familyInfo(uint32_t index, t5_font_family_info_t* out) {
  if (!active() || !out || index >= families.size()) return false;
  const auto& family = families[index];
  std::memset(out, 0, sizeof(*out));
  std::strncpy(out->name, family.name.c_str(), sizeof(out->name) - 1);
  std::strncpy(out->description, family.description.c_str(), sizeof(out->description) - 1);
  out->total_size = family.totalSize;
  out->installed = family.installed ? 1 : 0;
  out->has_update = family.hasUpdate ? 1 : 0;
  return true;
}

t5_font_result_t installFamily(uint32_t index, t5_font_progress_callback_t callback, void* ctx) {
  if (!active()) return T5_FONT_UNAVAILABLE;
  if (index >= families.size()) return T5_FONT_INVALID_INDEX;
  auto& family = families[index];
  if (!installer().ensureFamilyDir(family.name.c_str())) return T5_FONT_STORAGE_ERROR;

  for (size_t i = 0; i < family.files.size(); ++i) {
    const auto& entry = family.files[i];
    char path[128];
    FontInstaller::buildFontPath(family.name.c_str(), entry.name.c_str(), path, sizeof(path));
    const std::string url = baseUrl + entry.name;

    auto progress = [callback, ctx, &family, i](size_t downloaded, size_t total) {
      if (callback) callback(family.name.c_str(), static_cast<uint32_t>(i),
                             static_cast<uint32_t>(family.files.size()), downloaded, total, ctx);
    };
    const auto download = HttpDownloader::downloadToFile(url, path, progress);
    if (download != HttpDownloader::OK) {
      installer().deleteFamily(family.name.c_str());
      family.installed = false;
      family.hasUpdate = false;
      return T5_FONT_NETWORK_ERROR;
    }

    uint32_t crc = 0;
    if (!computeCrc32(path, crc)) {
      installer().deleteFamily(family.name.c_str());
      family.installed = false;
      family.hasUpdate = false;
      return T5_FONT_STORAGE_ERROR;
    }
    if (crc != entry.crc32) {
      installer().deleteFamily(family.name.c_str());
      family.installed = false;
      family.hasUpdate = false;
      return T5_FONT_CHECKSUM_ERROR;
    }
    if (!installer().validateCpfontFile(path)) {
      installer().deleteFamily(family.name.c_str());
      family.installed = false;
      family.hasUpdate = false;
      return T5_FONT_INVALID_FILE;
    }
  }

  installer().refreshRegistry();
  family.installed = true;
  family.hasUpdate = false;
  return T5_FONT_OK;
}

t5_font_result_t deleteFamily(uint32_t index) {
  if (!active()) return T5_FONT_UNAVAILABLE;
  if (index >= families.size()) return T5_FONT_INVALID_INDEX;
  auto& family = families[index];
  if (installer().deleteFamily(family.name.c_str()) != FontInstaller::Error::OK) return T5_FONT_STORAGE_ERROR;
  installer().refreshRegistry();
  family.installed = false;
  family.hasUpdate = false;
  return T5_FONT_OK;
}

const t5_font_api_v1 api = {
    T5_FONT_API_VERSION,
    sizeof(t5_font_api_v1),
    refreshCatalog,
    familyCount,
    familyInfo,
    installFamily,
    deleteFamily,
};

}  // namespace

extern "C" const t5_font_api_v1* t5_font_get_api(uint32_t version) {
  return version == T5_FONT_API_VERSION && active() ? &api : nullptr;
}
