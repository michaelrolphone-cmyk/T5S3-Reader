#include "FileAssociationRegistry.h"

#include "AppManifest.h"
#include "AppPackageInstaller.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "runtime/packages/PackageOrdinarySdAdapter.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace NativeFileAssociations {
namespace {

constexpr const char* kDirectory = "/.crosspoint";
constexpr const char* kManifest = "/.crosspoint/file-associations.json";
constexpr const char* kTemporary = "/.crosspoint/file-associations.json.tmp";
constexpr const char* kBackup = "/.crosspoint/file-associations.json.bak";
constexpr RuntimePackages::PackageRuntimePolicy kPolicy{
    "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};

std::vector<Handler> handlers;
bool loaded = false;

bool endsWith(const std::string& value, const char* suffix) {
  const size_t n = suffix ? std::strlen(suffix) : 0;
  return n && value.size() >= n &&
      value.compare(value.size() - n, n, suffix) == 0;
}

bool extensionForPath(const char* path, char (&out)[kExtensionBytes]) {
  out[0] = 0;
  if (!path || !path[0]) return false;
  const char* slash = std::strrchr(path, '/');
  const char* name = slash ? slash + 1 : path;
  const char* dot = std::strrchr(name, '.');
  if (!dot || dot == name) return false;
  const size_t length = std::strlen(dot);
  if (length < 2 || length >= sizeof(out)) return false;
  out[0] = '.';
  for (size_t i = 1; i < length; ++i) {
    const unsigned char ch = static_cast<unsigned char>(dot[i]);
    if (!std::isalnum(ch)) return false;
    out[i] = static_cast<char>(std::tolower(ch));
  }
  out[length] = 0;
  return true;
}

void copyText(char* out, size_t capacity, const char* value) {
  if (!out || !capacity) return;
  if (!value) value = "";
  std::snprintf(out, capacity, "%s", value);
}

bool addHandler(const char* extension, HandlerKind kind, const char* appId,
                const char* displayName, const char* icon, const char* launchPath) {
  if (!extension || !appId || !displayName || !icon ||
      std::strlen(extension) >= kExtensionBytes ||
      std::strlen(appId) >= kAppIdBytes ||
      std::strlen(displayName) >= kDisplayNameBytes ||
      std::strlen(icon) >= kIconBytes ||
      (launchPath && std::strlen(launchPath) >= kLaunchPathBytes) ||
      handlers.size() >= kMaxHandlers)
    return false;
  for (const auto& item : handlers)
    if (item.kind == kind && !std::strcmp(item.extension, extension) &&
        !std::strcmp(item.appId, appId))
      return true;
  Handler item{};
  item.kind = kind;
  copyText(item.extension, sizeof(item.extension), extension);
  copyText(item.appId, sizeof(item.appId), appId);
  copyText(item.displayName, sizeof(item.displayName), displayName);
  copyText(item.icon, sizeof(item.icon), icon);
  copyText(item.launchPath, sizeof(item.launchPath), launchPath);
  handlers.push_back(item);
  return true;
}

void addSystemReader() {
  static constexpr const char* types[] = {".epub", ".xtc", ".xtch", ".txt", ".md"};
  for (const char* type : types)
    (void)addHandler(type, HandlerKind::SystemReader, "riscrte-reader",
                     "Reader", "solid:f02d", "");
}

bool addManifestHandlers(const char* appId, const char* manifestPath,
                         const char* appRoot, const char* expectedArtifact,
                         bool legacy) {
  t5_app_manifest_t manifest{};
  AppFileTypes types{};
  if (!readAppManifest(manifestPath, manifest, nullptr, false, nullptr, &types) ||
      !manifest.compatible || !types.count)
    return false;
  if (expectedArtifact && std::strcmp(manifest.file_name, expectedArtifact))
    return false;

  std::string launch;
  if (legacy) {
    const std::string elf = std::string("/Apps/") + manifest.file_name;
    const std::string sidecar = std::string("/Apps/") + appId + ".json";
    if (!Storage.exists(elf.c_str()) ||
        !RuntimePackages::inspectInstalledAppPair(elf.c_str(), sidecar.c_str(),
                                                  manifest.file_name))
      return false;
    launch = "/sd" + elf;
  } else {
    const std::string elf = std::string(appRoot) + "/" + manifest.file_name;
    if (!Storage.exists(elf.c_str())) return false;
    launch = "/sd" + elf;
  }

  bool added = false;
  for (size_t i = 0; i < types.count; ++i)
    added = addHandler(types.values[i], HandlerKind::App, appId,
                       manifest.display_name, manifest.icon, launch.c_str()) || added;
  return added;
}

void scanCanonicalApp(const char* id) {
  if (!id || !RuntimePackages::safeId(id)) return;
  const std::string root = std::string("/Apps/") + id;
  RuntimePackages::Identity identity{};
  if (!RuntimePackages::inspectInstalledOrdinarySdDirectory(
          root.c_str(), kPolicy, RuntimePackages::installedCapabilityVersion, identity) ||
      identity.kind != RuntimePackages::Kind::Application ||
      std::strcmp(identity.id, id))
    return;

  HalFile directory = Storage.open(root.c_str(), O_RDONLY);
  if (!directory.isOpen() || !directory.isDirectory()) {
    if (directory.isOpen()) (void)directory.close();
    return;
  }
  while (true) {
    HalFile file = directory.openNextFile();
    if (!file.isOpen()) break;
    char name[128]{};
    const size_t length = file.getName(name, sizeof(name));
    const bool isDirectory = file.isDirectory();
    (void)file.close();
    if (isDirectory || !length || length >= sizeof(name)) continue;
    const std::string filename(name);
    if (filename == ".package.json" || !endsWith(filename, ".json")) continue;
    const std::string path = root + "/" + filename;
    if (addManifestHandlers(id, path.c_str(), root.c_str(), identity.artifact, false))
      break;
  }
  (void)directory.close();
}

void scanLegacyManifest(const char* name) {
  if (!name) return;
  const std::string filename(name);
  if (!endsWith(filename, ".json") || filename.size() <= 5) return;
  const std::string id = filename.substr(0, filename.size() - 5);
  if (!RuntimePackages::safeId(id.c_str())) return;
  const std::string path = "/Apps/" + filename;
  (void)addManifestHandlers(id.c_str(), path.c_str(), "/Apps", nullptr, true);
}

bool persist() {
  if (!Storage.ready() || !Storage.ensureDirectoryExists(kDirectory)) return false;

  JsonDocument doc;
  doc["schema"] = 1;
  JsonArray records = doc["associations"].to<JsonArray>();
  for (const auto& handler : handlers) {
    JsonObject record = records.add<JsonObject>();
    record["extension"] = handler.extension;
    record["kind"] = handler.kind == HandlerKind::App ? "app" : "system";
    record["app_id"] = handler.appId;
    record["display_name"] = handler.displayName;
    record["icon"] = handler.icon;
    if (handler.kind == HandlerKind::App) record["launch_path"] = handler.launchPath;
  }

  String json;
  serializeJson(doc, json);
  if (!Storage.writeFile(kTemporary, json)) return false;
  if (Storage.exists(kBackup)) (void)Storage.remove(kBackup);
  const bool hadManifest = Storage.exists(kManifest);
  if (hadManifest && !Storage.rename(kManifest, kBackup)) {
    (void)Storage.remove(kTemporary);
    return false;
  }
  if (!Storage.rename(kTemporary, kManifest)) {
    if (hadManifest) (void)Storage.rename(kBackup, kManifest);
    return false;
  }
  if (hadManifest) (void)Storage.remove(kBackup);
  return true;
}

}  // namespace

bool rebuild() {
  handlers.clear();
  loaded = false;
  if (!Storage.ready()) return false;
  addSystemReader();

  HalFile apps = Storage.open("/Apps", O_RDONLY);
  if (apps.isOpen() && apps.isDirectory()) {
    while (handlers.size() < kMaxHandlers) {
      HalFile entry = apps.openNextFile();
      if (!entry.isOpen()) break;
      char name[128]{};
      const size_t length = entry.getName(name, sizeof(name));
      const bool isDirectory = entry.isDirectory();
      (void)entry.close();
      if (!length || length >= sizeof(name)) continue;
      if (isDirectory) scanCanonicalApp(name);
      else scanLegacyManifest(name);
    }
    (void)apps.close();
  }

  std::sort(handlers.begin(), handlers.end(), [](const Handler& a, const Handler& b) {
    const int type = std::strcmp(a.extension, b.extension);
    if (type) return type < 0;
    const int name = std::strcmp(a.displayName, b.displayName);
    if (name) return name < 0;
    return std::strcmp(a.appId, b.appId) < 0;
  });

  loaded = true;
  const bool written = persist();
  if (!written) LOG_ERR("FILEOPEN", "Failed to persist file association manifest");
  return true;
}

void invalidate() { loaded = false; }

bool ensure() {
  if (loaded) return true;
  return rebuild();
}

uint32_t countForPath(const char* sdVfsPath) {
  if (!ensure()) return 0;
  char extension[kExtensionBytes]{};
  if (!extensionForPath(sdVfsPath, extension)) return 0;
  uint32_t count = 0;
  for (const auto& item : handlers)
    if (!std::strcmp(item.extension, extension)) ++count;
  return count;
}

bool getForPath(const char* sdVfsPath, uint32_t index, Handler& out) {
  out = {};
  if (!ensure()) return false;
  char extension[kExtensionBytes]{};
  if (!extensionForPath(sdVfsPath, extension)) return false;
  uint32_t current = 0;
  for (const auto& item : handlers) {
    if (std::strcmp(item.extension, extension)) continue;
    if (current++ == index) {
      out = item;
      return true;
    }
  }
  return false;
}

bool hasAppForPath(const char* sdVfsPath, const char* appId, Handler* out) {
  if (!appId || !ensure()) return false;
  char extension[kExtensionBytes]{};
  if (!extensionForPath(sdVfsPath, extension)) return false;
  for (const auto& item : handlers) {
    if (item.kind != HandlerKind::App || std::strcmp(item.extension, extension) ||
        std::strcmp(item.appId, appId))
      continue;
    if (out) *out = item;
    return true;
  }
  return false;
}

}  // namespace NativeFileAssociations
