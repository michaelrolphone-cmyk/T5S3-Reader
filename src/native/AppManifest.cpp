#include "AppManifest.h"
#include <AppManifestRules.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <cstring>

bool parseAppManifest(const std::string& json, t5_app_manifest_t& out) {
  out = {};
  if (json.empty() || json.size() > 2048 || json.find('\0') != std::string::npos) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc.is<JsonObject>()) return false;
  const char* keys[] = {"display_name", "file_name", "min_firmware_version", "icon"};
  char* fields[] = {out.display_name, out.file_name, out.min_firmware_version, out.icon};
  size_t sizes[] = {sizeof(out.display_name), sizeof(out.file_name), sizeof(out.min_firmware_version), sizeof(out.icon)};
  for (unsigned i = 0; i < 4; ++i) {
    if (!doc[keys[i]].is<const char*>()) return false;
    const char* value = doc[keys[i]].as<const char*>();
    const size_t n = std::strlen(value);
    if (!n || n >= sizes[i]) return false;
    for (size_t j = 0; j < n; ++j) if (static_cast<unsigned char>(value[j]) < 32) return false;
    std::memcpy(fields[i], value, n + 1);
  }
  uint32_t version[3], cp;
  bool regular;
  if (!t5_safe_elf_name(out.file_name) || !t5_parse_version(out.min_firmware_version, version, false) ||
      !t5_parse_icon(out.icon, &regular, &cp)) return false;
  out.compatible = t5_firmware_compatible(CROSSPOINT_VERSION, out.min_firmware_version);
  return true;
}
bool readAppManifest(const char* path, t5_app_manifest_t& out) {
  HalFile file = Storage.open(path, O_RDONLY);
  if (!file.isOpen() || file.isDirectory() || file.fileSize64() > 2048) return false;
  file.close();
  const String json = Storage.readFile(path);
  return parseAppManifest(std::string(json.c_str(), json.length()), out);
}
