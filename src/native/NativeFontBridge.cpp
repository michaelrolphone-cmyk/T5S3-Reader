#include <T5AppApi.h>
#include <T5FontApi.h>

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_rom_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "FontCatalogValidation.h"
#include "FontCatalogConfig.h"
#include "FontInstaller.h"
#include "SdCardFontGlobals.h"
#include "network/HttpDownloader.h"

namespace {

struct ManifestFile { std::string name; size_t size = 0; uint32_t crc32 = 0; };
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
FontInstaller& installer() { static FontInstaller value(sdFontSystem.registry()); return value; }

bool computeCrc32(const char* path, uint32_t& out) {
  // Hash each file once, with bounded memory, bytes and elapsed time. Do not
  // accept premature EOF as a complete checksum when media disappears.
  constexpr uint64_t kMaxFontBytes = 64u * 1024u * 1024u;
  constexpr uint32_t kDeadlineMs = 30000;
  FsFile f;
  if (!Storage.openFileForRead("FONT", path, f)) return false;
  const uint64_t expected = f.fileSize();
  if (expected > kMaxFontBytes) { f.close(); return false; }
  uint8_t buf[1024];
  uint32_t crc = 0;
  uint64_t processed = 0;
  uint32_t checkpointBytes = 0;
  const uint32_t started = millis();
  uint32_t checkpoint = started, lastProgress = started;
  while (processed < expected) {
    const uint32_t now = millis();
    if (now - started >= kDeadlineMs) {
      LOG_ERR("FONT", "CRC timed out: %s", path);
      f.close();
      return false;
    }
    const size_t count = expected - processed < sizeof(buf) ?
        static_cast<size_t>(expected - processed) : sizeof(buf);
    const int n = f.read(buf, count);
    if (n <= 0 || static_cast<size_t>(n) > count) {
      f.close();
      return false;
    }
    crc = esp_rom_crc32_le(crc, buf, static_cast<uint32_t>(n));
    processed += static_cast<uint32_t>(n);
    checkpointBytes += static_cast<uint32_t>(n);
    const uint32_t afterRead = millis();
    if (checkpointBytes >= 4096 || afterRead - checkpoint >= 10) {
      if (afterRead - lastProgress >= 1000) {
        LOG_DBG("FONT", "CRC %s: %lu/%lu bytes", path,
                static_cast<unsigned long>(processed), static_cast<unsigned long>(expected));
        lastProgress = afterRead;
      }
      vTaskDelay(1);
      checkpointBytes = 0;
      checkpoint = millis();
    }
  }
  const bool complete = f.fileSize() == expected && millis() - started < kDeadlineMs;
  f.close();
  if (!complete) return false;
  out = crc;
  return true;
}

t5_font_result_t refreshCatalog() {
  if (!active()) return T5_FONT_UNAVAILABLE;
  static constexpr const char* tempPath = "/fonts_manifest.tmp";
  const auto download = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, tempPath, nullptr);
  if (download != HttpDownloader::OK) { Storage.remove(tempPath); return T5_FONT_NETWORK_ERROR; }
  FsFile file;
  if (!Storage.openFileForRead("FONT", tempPath, file)) { Storage.remove(tempPath); return T5_FONT_STORAGE_ERROR; }
  JsonDocument doc;
  const auto err = deserializeJson(doc, file);
  file.close();
  Storage.remove(tempPath);
  if (err || (doc["version"] | 0) != FONTS_MANIFEST_VERSION) return T5_FONT_MANIFEST_ERROR;
  const bool familiesFieldIsArray = doc["families"].is<JsonArray>();
  if (!familiesFieldIsArray) return T5_FONT_MANIFEST_ERROR;
  std::string candidateBaseUrl = doc["baseUrl"] | "";
  std::vector<ManifestFamily> candidateFamilies;
  installer().refreshRegistry();
  for (JsonObject fObj : doc["families"].as<JsonArray>()) {
    ManifestFamily family;
    family.name = fObj["name"] | "";
    family.description = fObj["description"] | "";
    if (!FontInstaller::isValidFamilyName(family.name.c_str())) return T5_FONT_MANIFEST_ERROR;
    if (!fObj["files"].is<JsonArray>()) return T5_FONT_MANIFEST_ERROR;
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      ManifestFile entry;
      entry.name = fileObj["name"] | "";
      entry.size = fileObj["size"] | 0;
      if (!FontInstaller::isValidCpfontFilename(entry.name.c_str()) || !fileObj["crc32"].is<uint32_t>()) return T5_FONT_MANIFEST_ERROR;
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
        if (!Storage.openFileForRead("FONT", path, installedFile)) { family.hasUpdate = true; break; }
        const size_t actual = installedFile.fileSize();
        installedFile.close();
        if (actual != entry.size) { family.hasUpdate = true; break; }
        uint32_t installedCrc = 0;
        if (!computeCrc32(path, installedCrc) || installedCrc != entry.crc32) {
          family.hasUpdate = true;
          break;
        }
      }
    }
    candidateFamilies.push_back(std::move(family));
  }
  if (!FontCatalogValidation::publish(families, candidateFamilies, familiesFieldIsArray)) {
    return T5_FONT_MANIFEST_ERROR;
  }
  baseUrl.swap(candidateBaseUrl);
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
      if (callback) callback(family.name.c_str(), static_cast<uint32_t>(i), static_cast<uint32_t>(family.files.size()), downloaded, total, ctx);
    };
    const auto download = HttpDownloader::downloadToFile(url, path, progress);
    if (download != HttpDownloader::OK) { installer().deleteFamily(family.name.c_str()); family.installed = false; family.hasUpdate = false; return T5_FONT_NETWORK_ERROR; }
    uint32_t crc = 0;
    if (!computeCrc32(path, crc)) { installer().deleteFamily(family.name.c_str()); family.installed = false; family.hasUpdate = false; return T5_FONT_STORAGE_ERROR; }
    if (crc != entry.crc32) { installer().deleteFamily(family.name.c_str()); family.installed = false; family.hasUpdate = false; return T5_FONT_CHECKSUM_ERROR; }
    if (!installer().validateCpfontFile(path)) { installer().deleteFamily(family.name.c_str()); family.installed = false; family.hasUpdate = false; return T5_FONT_INVALID_FILE; }
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

uint32_t choiceCount() {
  if (!active()) return 0;
  return CrossPointSettings::BUILTIN_FONT_COUNT + static_cast<uint32_t>(sdFontSystem.registry().getFamilyCount());
}

bool choiceInfo(uint32_t index, t5_font_choice_info_t* out) {
  if (!active() || !out || index >= choiceCount()) return false;
  std::memset(out, 0, sizeof(*out));
  if (index < CrossPointSettings::BUILTIN_FONT_COUNT) {
    std::strncpy(out->name, index == 0 ? "Noto Serif" : "Noto Sans", sizeof(out->name) - 1);
    out->builtin = 1;
    out->selected = SETTINGS.sdFontFamilyName[0] == '\0' && SETTINGS.fontFamily == index;
    return true;
  }
  const auto& fontFamilies = sdFontSystem.registry().getFamilies();
  const uint32_t sdIndex = index - CrossPointSettings::BUILTIN_FONT_COUNT;
  if (sdIndex >= fontFamilies.size()) return false;
  std::strncpy(out->name, fontFamilies[sdIndex].name.c_str(), sizeof(out->name) - 1);
  out->selected = SETTINGS.sdFontFamilyName[0] != '\0' && fontFamilies[sdIndex].name == SETTINGS.sdFontFamilyName;
  return true;
}

t5_font_result_t selectChoice(uint32_t index) {
  if (!active() || index >= choiceCount()) return T5_FONT_INVALID_INDEX;

  const uint8_t previousFontFamily = SETTINGS.fontFamily;
  char previousSdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName)];
  std::memcpy(previousSdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(previousSdFontFamilyName));

  if (index < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = static_cast<uint8_t>(index);
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else {
    const auto& fontFamilies = sdFontSystem.registry().getFamilies();
    const uint32_t sdIndex = index - CrossPointSettings::BUILTIN_FONT_COUNT;
    if (sdIndex >= fontFamilies.size()) return T5_FONT_INVALID_INDEX;
    std::strncpy(SETTINGS.sdFontFamilyName, fontFamilies[sdIndex].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  }

  if (!SETTINGS.saveToFile()) {
    SETTINGS.fontFamily = previousFontFamily;
    std::memcpy(SETTINGS.sdFontFamilyName, previousSdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName));
    ensureSdFontLoaded();
    LOG_ERR("FONT", "Failed to persist font family selection");
    return T5_FONT_STORAGE_ERROR;
  }

  ensureSdFontLoaded();
  return T5_FONT_OK;
}

const t5_font_api_v1 api = {
    T5_FONT_API_VERSION, sizeof(t5_font_api_v1), refreshCatalog, familyCount, familyInfo,
    installFamily, deleteFamily, choiceCount, choiceInfo, selectChoice,
};

}  // namespace

extern "C" const t5_font_api_v1* t5_font_get_api(uint32_t version) {
  return version == T5_FONT_API_VERSION && active() ? &api : nullptr;
}
