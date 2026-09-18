#!/usr/bin/env python3
"""Patch the real native App Store release path to use the canonical installer.

This is an explicit one-shot source edit run on the PR branch by the scoped
workflow. The script is deliberately idempotent and refuses unknown layouts.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / 'src/native/NativeAppHost.cpp'
START = '\nbool appCatalogDownload(uint32_t index) {'
END = '\nbool installedRefresh() {'
INCLUDE = '#include "NativeOnlineAppInstall.h"\n'
REPLACEMENT = '''
bool appCatalogDownload(uint32_t index) {
  auto* s = current();
  if (!s || !Storage.ready() || index >= s->catalog.size()) return false;
  const CatalogAsset selected = s->catalog[index];
  if (!safeAssetName(selected.name) ||
      !RuntimePackages::safePackageEntryName(selected.name.c_str()) ||
      !selected.manifestValid || selected.manifestUrl.empty() ||
      selected.size < 52 || selected.size > 8u * 1024u * 1024u) return false;

  std::string json, version;
  t5_app_manifest_t manifest{};
  if (!HttpDownloader::fetchUrl(selected.manifestUrl, json) ||
      json.empty() || json.size() > 4096 ||
      !parseAppManifest(json, manifest, &version, true) ||
      !manifest.compatible || selected.name != manifest.file_name ||
      version != selected.version) return false;
  JsonDocument metadata;
  if (deserializeJson(metadata, json) || !metadata.is<JsonObjectConst>() ||
      !metadata["sha256"].is<const char*>() ||
      !metadata["size_bytes"].is<unsigned>() ||
      metadata["size_bytes"].as<unsigned>() != selected.size) {
    LOG_ERR("APPSTORE", "Release metadata lacks matching executable length and digest");
    return false;
  }
  const char* digest = metadata["sha256"].as<const char*>();
  if (!RuntimePackages::validSha256Hex(digest)) return false;
  char installed[T5_APP_VERSION_MAX]{};
  if (installedAppVersionGet(selected.name.c_str(), installed, sizeof(installed)) &&
      RuntimePackages::comparePackageVersions(version.c_str(), installed) !=
          RuntimePackages::VersionOrder::Newer) return false;

  // There is exactly one publication mechanism for all four ordinary package
  // kinds. A release is first converted to an exclusive canonical SD source;
  // that source is independently verified by the shared transaction engine.
  return RuntimeOnlinePackages::installApplication(selected.name.c_str(),
      version.c_str(), selected.url.c_str(), json, selected.size, digest);
}
'''


def main() -> None:
    before = PATH.read_text(encoding='utf-8')
    if 'RuntimeOnlinePackages::installApplication(' in before:
        print('Canonical online App Store path already present')
        return
    if before.count(START) != 1 or before.count(END) != 1 or before.count(INCLUDE) > 1:
        raise SystemExit('Unexpected native App Store layout: refusing source mutation')
    begin = before.index(START)
    end = before.index(END, begin)
    after = before[:begin] + REPLACEMENT + before[end:]
    if INCLUDE not in after:
        after = after.replace('#include "NativeAppHost.h"\n',
                              '#include "NativeAppHost.h"\n' + INCLUDE, 1)
    if after == before or after.count('RuntimeOnlinePackages::installApplication(') != 1:
        raise SystemExit('Canonical online App Store mutation failed')
    PATH.write_text(after, encoding='utf-8')
    print('Migrated actual NativeAppHost.cpp release download to canonical app package engine')


if __name__ == '__main__':
    main()
