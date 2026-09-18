#!/usr/bin/env python3
"""One-shot source migration of native app inventory and launch to canonical packages.

Fails on unexpected upstream layout rather than modifying an unrelated block.
The resulting source, not this build-time script, is committed by the scoped
PR branch migration workflow. Safe to run locally with --write.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/native/NativeAppHost.cpp'


def section(source: str, start: str, end: str, replacement: str) -> str:
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError(f'Unexpected source layout for {start!r} / {end!r}')
    first = source.index(start)
    last = source.index(end, first)
    return source[:first] + replacement + source[last:]


def migrate(source: str) -> str:
    mark = '#include "AppPackageInstaller.h"\n'
    if source.count(mark) != 1:
        raise ValueError('Expected one AppPackageInstaller include')
    source = source.replace(mark, mark +
        '#include "runtime/packages/PackageOrdinarySdAdapter.h"\n'
        '#include "runtime/packages/InstalledCapabilityResolver.h"\n'
        '#include "runtime/packages/PackageUseGate.h"\n', 1)
    source = section(source, '\nbool installedAppVersionGet(', '\nbool appCatalogDownload(', '''
namespace {
constexpr RuntimePackages::PackageRuntimePolicy kCanonicalAppPolicy{
    "xtensa-esp32s3", 2, 0, 8u * 1024u * 1024u, 16u * 1024u * 1024u};

bool verifiedManagedApp(const char* id, RuntimePackages::Identity& identity,
                        t5_app_manifest_t* manifest = nullptr) {
  identity = {};
  if (!id || !RuntimePackages::safeId(id)) return false;
  const std::string root = std::string("/Apps/") + id;
  if (!RuntimePackages::verifyOrdinarySdDirectory(root.c_str(), kCanonicalAppPolicy,
          RuntimePackages::installedCapabilityVersion, identity) ||
      identity.kind != RuntimePackages::Kind::Application ||
      std::strcmp(identity.id, id) || !t5_safe_elf_name(identity.artifact))
    return false;
  const std::string elf = root + "/" + identity.artifact;
  const std::string json = elf.substr(0, elf.size() - 4) + ".json";
  if (!Storage.exists(elf.c_str()) || !Storage.exists(json.c_str())) return false;
  t5_app_manifest_t parsed{};
  if (!readAppManifest(json.c_str(), parsed) ||
      std::strcmp(parsed.file_name, identity.artifact)) return false;
  if (manifest) *manifest = parsed;
  return true;
}
} // namespace

bool installedAppVersionGet(const char* fileName, char* out, size_t capacity) {
  auto* s = current();
  if (!s || !Storage.ready() || !t5_safe_elf_name(fileName) || !out || !capacity)
    return false;
  const std::string id(fileName, std::strlen(fileName) - 4);
  RuntimePackages::Identity canonical{};
  if (verifiedManagedApp(id.c_str(), canonical) &&
      std::strcmp(canonical.artifact, fileName) == 0)
    return copyVersion(canonical.version, out, capacity);
  const std::string destination = std::string("/Apps/") + fileName;
  const std::string sidecar = destination.substr(0, destination.size() - 4) + ".json";
  if (!RuntimePackages::recoverAppPair(fileName) ||
      !Storage.exists(destination.c_str()) || !Storage.exists(sidecar.c_str()) ||
      !RuntimePackages::verifyAppPair(destination.c_str(), sidecar.c_str(), fileName, false))
    return false;
  t5_app_manifest_t manifest{};
  std::string version;
  if (!readAppManifest(sidecar.c_str(), manifest, &version, false) ||
      std::strcmp(manifest.file_name, fileName)) return false;
  return copyVersion(version, out, capacity);
}
''')
    source = section(source, '\nbool installedRefresh() {', '\nuint32_t installedCount()', '''
bool installedRefresh() {
  auto* s = current();
  if (!s) return false;
  s->installed.clear();
  if (!RuntimePackages::recoverAppInventory())
    LOG_ERR("APPSTORE", "Some legacy app updates require manual recovery");
  HalFile dir = Storage.open("/Apps", O_RDONLY);
  if (!dir.isOpen() || !dir.isDirectory()) return false;
  while (s->installed.size() < 128) {
    esp_task_wdt_reset();
    HalFile file = dir.openNextFile();
    if (!file.isOpen()) break;
    char name[128]{};
    file.getName(name, sizeof(name));
    const bool isDir = file.isDirectory();
    file.close();
    const std::string filename(name);
    if (isDir) {
      RuntimePackages::Identity identity{};
      t5_app_manifest_t manifest{};
      if (!verifiedManagedApp(name, identity, &manifest) ||
          !std::strcmp(manifest.file_name, "springboard.elf")) continue;
      s->installed.push_back(manifest);
      continue;
    }
    if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".json") continue;
    t5_app_manifest_t manifest{};
    if (!readAppManifest((std::string("/Apps/") + filename).c_str(), manifest)) continue;
    if (filename != std::string(manifest.file_name).substr(0, std::strlen(manifest.file_name) - 4) + ".json") continue;
    if (!std::strcmp(manifest.file_name, "springboard.elf")) continue;
    if (!Storage.exists((std::string("/Apps/") + manifest.file_name).c_str())) continue;
    if (Storage.exists((std::string("/Apps/") + manifest.file_name + ".bak").c_str()) ||
        Storage.exists((std::string("/Apps/") + filename + ".bak").c_str())) continue;
    s->installed.push_back(manifest);
  }
  dir.close();
  std::sort(s->installed.begin(), s->installed.end(), [](const t5_app_manifest_t& a, const t5_app_manifest_t& b) {
    return std::strcmp(a.display_name, b.display_name) < 0;
  });
  return true;
}
''')
    source = section(source, '\nbool requestLaunch(uint32_t index) {', '\nbool drawIcon(', '''
bool requestLaunch(uint32_t index) {
  auto* s = current();
  if (!s || s->exiting || index >= s->installed.size() || !s->installed[index].compatible)
    return false;
  const char* artifact = s->installed[index].file_name;
  const std::string id(artifact, std::strlen(artifact) - 4);
  RuntimePackages::Identity managed{};
  if (verifiedManagedApp(id.c_str(), managed) &&
      !std::strcmp(managed.artifact, artifact))
    s->launchPath = std::string("/sd/Apps/") + id + "/" + artifact;
  else
    s->launchPath = std::string("/sd/Apps/") + artifact;
  s->exiting = true;
  return true;
}
''')
    original = '''  const std::string filename = elf.substr(elf.find_last_of('/') + 1);
  const std::string sidecar = elf.substr(0, elf.size() - 4) + ".json";
'''
    addition = '''  const std::string filename = elf.substr(elf.find_last_of('/') + 1);
  const std::string sidecar = elf.substr(0, elf.size() - 4) + ".json";
  // Nested /Apps/<id>/<artifact> entries are independently verified against
  // their exact canonical package inventory. A failed verification never
  // falls through to the loose-file compatibility loader.
  const size_t nestedSlash = elf.compare(0, 6, "/Apps/") == 0 ?
      elf.find('/', 6) : std::string::npos;
  std::string canonicalRoot;
  if (nestedSlash != std::string::npos) {
    const std::string id = elf.substr(6, nestedSlash - 6);
    RuntimePackages::Identity identity{};
    if (!verifiedManagedApp(id.c_str(), identity) ||
        std::strcmp(identity.artifact, filename.c_str())) {
      lastLaunchError = "Canonical application package or executable is invalid.";
      return ESP_ERR_NOT_SUPPORTED;
    }
    canonicalRoot = elf.substr(0, nestedSlash);
  }
'''
    if source.count(original) != 1:
        raise ValueError('Cannot find app launch filename block')
    source = source.replace(original, addition, 1)
    original = '''  if (elf.compare(0, 6, "/Apps/") == 0 && t5_safe_elf_name(filename.c_str()) &&
      !RuntimePackages::recoverAppPair(filename.c_str())) {'''
    amended = '''  if (elf.compare(0, 6, "/Apps/") == 0 &&
      nestedSlash == std::string::npos && t5_safe_elf_name(filename.c_str()) &&
      !RuntimePackages::recoverAppPair(filename.c_str())) {'''
    if source.count(original) != 1:
        raise ValueError('Cannot find legacy recovery boundary')
    source = source.replace(original, amended, 1)
    original = '''  nativeStreamsBegin();
  const esp_err_t result = launch_elf_app(path);
  nativeStreamsEnd();'''
    amended = '''  // Pin the exact installed generation before mapping and release only after
  // the ELF has returned and dlclose has succeeded. Failed unloads retain the
  // pin; replacement and uninstall must not race executable memory.
  if (!canonicalRoot.empty() &&
      !RuntimePackages::systemPackageUseGate().pin(canonicalRoot.c_str())) {
    session = nullptr;
    nativeSystemUiEnd();
    nativeSettingsEnd();
    lastLaunchError = "Managed application is being replaced or is unavailable.";
    return ESP_ERR_INVALID_STATE;
  }
  nativeStreamsBegin();
  const esp_err_t result = launch_elf_app(path);
  nativeStreamsEnd();
  if (!canonicalRoot.empty() && result == ESP_OK)
    (void)RuntimePackages::systemPackageUseGate().unpin(canonicalRoot.c_str());'''
    if source.count(original) != 1:
        raise ValueError('Cannot find managed ELF lifecycle hook')
    source = source.replace(original, amended, 1)
    return source


def main() -> None:
    original = SOURCE.read_text(encoding='utf-8')
    altered = migrate(original)
    if altered == original:
        raise ValueError('Migration did not alter source')
    if '--write' not in sys.argv:
        print('Migration preflight OK; use --write to update NativeAppHost.cpp')
        return
    SOURCE.write_text(altered, encoding='utf-8')
    print('Canonical installed app discovery/version/launch and ELF mapping pins migrated.')


if __name__ == '__main__':
    main()
