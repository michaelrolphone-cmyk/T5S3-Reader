#!/usr/bin/env python3
"""Cross-layer contract for native file associations and common file handoff."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

registry = (ROOT / "src/native/FileAssociationRegistry.cpp").read_text(encoding="utf-8")
bridge = (ROOT / "src/native/NativeFileOpenBridge.cpp").read_text(encoding="utf-8")
launcher = (ROOT / "lib/NativeApps/src/NativeAppLauncher.c").read_text(encoding="utf-8")
browser = (ROOT / "Apps/file_browser.c").read_text(encoding="utf-8")
image = (ROOT / "Apps/image_viewer.c").read_text(encoding="utf-8")
editor = (ROOT / "Apps/text_editor.c").read_text(encoding="utf-8")
online = (ROOT / "src/native/NativeOnlineAppInstall.h").read_text(encoding="utf-8")
packages = (ROOT / "src/native/NativePackageManagerBridge.cpp").read_text(encoding="utf-8")

image_manifest = json.loads((ROOT / "Apps/image_viewer.json").read_text(encoding="utf-8"))
editor_manifest = json.loads((ROOT / "Apps/text_editor.json").read_text(encoding="utf-8"))

assert image_manifest["supported_file_types"] == [".jpg", ".jpeg", ".png", ".bmp"]
assert editor_manifest["supported_file_types"] == [".txt", ".md"]

assert '/System/Registry/FileAssociations.json' in registry
assert '/System/State/Applications/file_browser/Session.txt' in browser
assert '/sd/.crosspoint/file_browser.session' not in browser
assert '"riscrte-reader"' in registry
for extension in (".epub", ".xtc", ".xtch", ".txt", ".md"):
    assert f'"{extension}"' in registry
assert "readAppManifest" in registry
assert "inspectInstalledOrdinarySdDirectory" in registry
assert "inspectInstalledAppPair" in registry

assert "t5_file_open_get_api" in launcher
assert "t5_file_open_get_api" in bridge
assert "sourcePathGet" in bridge
assert "NativeFileOpenActivity" in bridge

assert "file_open->handler_count" in browser
assert "file_open->handler_get" in browser
assert "file_open->open_request" in browser
assert '"Open with"' in browser
assert '"No registered app for this file type"' in browser

assert "file_open->source_path_get" in image
assert "file_open->source_path_get" in editor

assert "NativeFileAssociations::rebuild()" in online
assert packages.count("NativeFileAssociations::rebuild()") >= 3

print("File associations: metadata, generated manifest, mutation hooks, chooser and common handoff PASS")

assert '/.crosspoint/file-associations.json' not in registry
