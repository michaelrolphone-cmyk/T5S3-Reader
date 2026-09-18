#!/usr/bin/env python3
"""Require canonical online installers to use the existing stream download API.

HttpDownloader routes only .part destinations through the new lossless
HTTP -> stream -> exclusively created SD file pipeline. Downloading directly
to an ELF filename would silently fall back to the deprecated HTTPClient path.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'src/native/NativeOnlineAppInstall.h'
DRIVER = ROOT / 'src/native/NativeOnlineDriverInstall.h'


def patch(path: Path, old: str, new: str, marker: str) -> None:
    source = path.read_text(encoding='utf-8')
    if marker in source:
        print(f'Streamed intake already wired: {path.name}')
        return
    if source.count(old) != 1:
        raise SystemExit(f'Unknown installer layout; refusing mutation: {path}')
    path.write_text(source.replace(old, new, 1), encoding='utf-8')
    print(f'Wired exclusive .part stream intake: {path.name}')


patch(APP, '''  const std::string elfPath = root + "/" + artifact;
  if (HttpDownloader::downloadToFile(url, elfPath,
          [](size_t, size_t) { esp_task_wdt_reset(); }) != HttpDownloader::OK ||
      !verifyAppPair(elfPath.c_str(), jsonPath.c_str(), artifact, true)) return false;
''', '''  const std::string elfPath = root + "/" + artifact;
  const std::string elfStage = elfPath + ".part";
  // Only a .part destination uses the lossless HTTP -> stream -> exclusive SD
  // writer. Commit that file inside our exclusively owned source directory.
  if (HttpDownloader::downloadToFile(url, elfStage,
          [](size_t, size_t) { esp_task_wdt_reset(); }) != HttpDownloader::OK ||
      Storage.exists(elfPath.c_str()) ||
      !Storage.rename(elfStage.c_str(), elfPath.c_str()) ||
      !verifyAppPair(elfPath.c_str(), jsonPath.c_str(), artifact, true)) return false;
''', 'const std::string elfStage = elfPath + ".part";')

patch(DRIVER, '''        const std::string target = root + "/" + kNames[i];
        if (HttpDownloader::downloadToFile(prefix + kNames[i], target,
                [](size_t, size_t) { esp_task_wdt_reset(); }) != HttpDownloader::OK)
            return false;
''', '''        const std::string target = root + "/" + kNames[i];
        const std::string stage = target + ".part";
        if (HttpDownloader::downloadToFile(prefix + kNames[i], stage,
                [](size_t, size_t) { esp_task_wdt_reset(); }) != HttpDownloader::OK ||
            Storage.exists(target.c_str()) ||
            !Storage.rename(stage.c_str(), target.c_str()))
            return false;
''', 'const std::string stage = target + ".part";')
