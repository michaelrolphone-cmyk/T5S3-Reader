#include "AppManifest.h"
#include <AppManifestRules.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include "runtime/packages/PackageIdentity.h"
#include "runtime/packages/PackageJsonGuard.h"
#include <cstring>

#ifndef CROSSPOINT_COMPAT_VERSION
#define CROSSPOINT_COMPAT_VERSION CROSSPOINT_VERSION
#endif

bool parseAppManifest(const std::string& json, t5_app_manifest_t& out,
                      std::string* appVersion, bool requireAppVersion,
                      RuntimeDevices::AppCapabilityRequirements* requirements) {
  out = {};
  if (appVersion) appVersion->clear();
  if (requirements) *requirements = {};
  if (json.empty() || json.size() > 2048 || json.find('\0') != std::string::npos ||
      !RuntimePackages::safePackageJsonObject(json.data(), json.size())) return false;
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

  const JsonVariantConst versionNode = doc["version"];
  if (versionNode.is<const char*>()) {
    const char* value = versionNode.as<const char*>();
    const size_t n = std::strlen(value);
    uint32_t parsed[3];
    if (!n || n >= T5_APP_VERSION_MAX || !t5_parse_version(value, parsed, false)) return false;
    for (size_t j = 0; j < n; ++j) if (static_cast<unsigned char>(value[j]) < 32) return false;
    if (appVersion) appVersion->assign(value, n);
  } else if (!versionNode.isNull()) {
    return false;
  }

  // Legacy manifests may omit both integrity declarations. New release
  // manifests provide both; neither field grants trust without hashing the
  // actual ELF and authenticating the manifest through an independent policy.
  const JsonVariantConst sizeNode = doc["size_bytes"];
  const JsonVariantConst digestNode = doc["sha256"];
  if (sizeNode.isNull() != digestNode.isNull()) return false;
  if (!sizeNode.isNull()) {
    if (!sizeNode.is<unsigned>() || !digestNode.is<const char*>()) return false;
    const unsigned bytes = sizeNode.as<unsigned>();
    // Match both legacy pair verification and ordinary application packages.
    if (bytes < 52 || bytes > 8u * 1024u * 1024u) return false;
    const char* digest = digestNode.as<const char*>();
    if (std::strlen(digest) != 64) return false;
    for (unsigned i = 0; i < 64; ++i) {
      if (!((digest[i] >= '0' && digest[i] <= '9') ||
            (digest[i] >= 'a' && digest[i] <= 'f'))) return false;
    }
  }

  // Runtime parsing intentionally accepts pre-versioning sidecars so firmware can
  // still browse/install the currently published legacy release. The release build
  // validator requires version for every newly published app. A missing runtime
  // version is represented by an empty string and compares equal only to another
  // legacy sidecar; the first versioned catalog release will therefore offer Update.
  (void)requireAppVersion;

  // A requirement is a capability ID and an explicit minimum API major
  // version. Unknown fields are rejected rather than silently ignoring future
  // constraints that could change security or launch semantics.
  auto parseList = [&](const char* key,
                       RuntimeDevices::AppCapabilityRequirements& parsed) -> bool {
    const JsonVariantConst value = doc[key];
    if (value.isNull()) return true;  // Old manifests did not declare capabilities.
    // Read-only variants cannot expose mutable ArduinoJson containers; testing
    // JsonArray/JsonObject here rejects even valid empty capability lists.
    if (!value.is<JsonArrayConst>()) return false;
    const JsonArrayConst entries = value.as<JsonArrayConst>();
    if (entries.size() > RuntimeDevices::kMaxAppRequirements) return false;
    for (JsonVariantConst item : entries) {
      if (!item.is<JsonObjectConst>()) return false;
      const JsonObjectConst record = item.as<JsonObjectConst>();
      if (record.size() != 2 || !record["capability"].is<const char*>() ||
          !record["api"].is<const char*>()) return false;
      if (!RuntimeDevices::addRequirement(&parsed, record["capability"].as<const char*>(),
                                          record["api"].as<const char*>())) return false;
    }
    return true;
  };
  RuntimeDevices::AppCapabilityRequirements mandatory{};
  RuntimeDevices::AppCapabilityRequirements optional{};
  if (!parseList("requires", mandatory) || !parseList("optional", optional)) return false;
  for (size_t i = 0; i < mandatory.count; ++i)
    for (size_t j = 0; j < optional.count; ++j)
      if (std::strcmp(mandatory.entries[i].capability, optional.entries[j].capability) == 0)
        return false;

  uint32_t version[3], cp;
  bool regular;
  if (!t5_safe_elf_name(out.file_name) || !t5_parse_version(out.min_firmware_version, version, false) ||
      !t5_parse_icon(out.icon, &regular, &cp)) return false;

  // New typed manifests share the same bounded package identity contract as
  // drivers. Existing untyped app sidecars derive a stable ID from the ELF
  // basename and may lack a version; neither form implies signing or trust.
  const JsonVariantConst typeNode = doc["type"];
  if (!typeNode.isNull() && (!typeNode.is<const char*>() ||
      std::strcmp(typeNode.as<const char*>(), "application") != 0)) return false;
  const JsonVariantConst idNode = doc["id"];
  if (!idNode.isNull() && !idNode.is<const char*>()) return false;
  RuntimePackages::Identity identity{};
  if (!RuntimePackages::makeIdentity(RuntimePackages::Kind::Application,
      idNode.isNull() ? nullptr : idNode.as<const char*>(),
      versionNode.isNull() ? nullptr : versionNode.as<const char*>(),
      out.file_name, true, &identity)) return false;

  // Compatibility must use the clean semantic release version, not the display/build
  // version. Development and RC builds append branch/hash metadata to CROSSPOINT_VERSION;
  // CROSSPOINT_COMPAT_VERSION is always the bare major.minor.patch firmware floor.
  out.compatible = t5_firmware_compatible(CROSSPOINT_COMPAT_VERSION, out.min_firmware_version);
  if (requirements) *requirements = mandatory;
  return true;
}
bool readAppManifest(const char* path, t5_app_manifest_t& out,
                     std::string* appVersion, bool requireAppVersion,
                     RuntimeDevices::AppCapabilityRequirements* requirements) {
  if (requirements) *requirements = {};
  HalFile file = Storage.open(path, O_RDONLY);
  if (!file.isOpen() || file.isDirectory() || file.fileSize64() > 2048) return false;
  file.close();
  const String json = Storage.readFile(path);
  return parseAppManifest(std::string(json.c_str(), json.length()), out, appVersion,
                          requireAppVersion, requirements);
}
