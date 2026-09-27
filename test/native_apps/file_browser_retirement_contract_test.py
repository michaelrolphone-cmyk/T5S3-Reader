#!/usr/bin/env python3
"""Lock retirement of the firmware-native general file browser."""

import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
ACTIVITY_MANAGER_CPP = (ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")
ACTIVITY_MANAGER_H = (ROOT / "src/activities/ActivityManager.h").read_text(encoding="utf-8")
SD_UPDATE = (ROOT / "src/activities/settings/SdFirmwareUpdateActivity.cpp").read_text(encoding="utf-8")
SD_BRIDGE = (ROOT / "src/native/NativeSdFirmwareBridge.cpp").read_text(encoding="utf-8")
FILE_OPEN = (ROOT / "src/native/NativeFileOpenBridge.cpp").read_text(encoding="utf-8")
MANIFEST = json.loads((ROOT / "Apps/sd_firmware_update.json").read_text(encoding="utf-8"))
READER = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")
INSTALLED_APP = (ROOT / "src/activities/util/InstalledAppActivity.cpp").read_text(encoding="utf-8")


class FileBrowserRetirementContract(unittest.TestCase):
    def test_general_firmware_file_browser_is_removed(self):
        self.assertFalse((ROOT / "src/activities/home/FileBrowserActivity.cpp").exists())
        self.assertFalse((ROOT / "src/activities/home/FileBrowserActivity.h").exists())
        for path in (ROOT / "src").rglob("*"):
            if path.suffix not in {".cpp", ".h"}:
                continue
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("FileBrowserActivity", text, str(path))
            self.assertNotIn("goToFileBrowser", text, str(path))

    def test_reader_file_selection_uses_installed_file_browser(self):
        self.assertNotIn("goToFileBrowser", READER)
        self.assertIn('goToInstalledApp("file_browser.elf", "File Browser")', READER)
        self.assertIn("resolveInstalledAppPath(artifact.c_str()", INSTALLED_APP)
        self.assertIn("RequiredAppActivity", INSTALLED_APP)
        self.assertIn("runNativeApp(path.c_str()", INSTALLED_APP)

    def test_settings_firmware_update_delegates_to_file_browser_app(self):
        self.assertNotIn("FileBrowserActivity", SD_UPDATE)
        self.assertIn('resolveInstalledAppPath("file_browser.elf"', SD_UPDATE)
        self.assertIn('"Select a .bin file to update firmware."', SD_UPDATE)
        self.assertIn("RequiredAppActivity", SD_UPDATE)
        self.assertIn("installedUpdaterHasBinAssociation", SD_UPDATE)
        self.assertIn("updaterInstalled)", SD_UPDATE)
        self.assertIn("NativeFileAssociations::rebuild()", SD_UPDATE)
        self.assertIn("nativeSystemUiSetHomeNavigationSuppressed(true)", SD_UPDATE)
        self.assertIn("nativeSystemUiSetHomeNavigationSuppressed(false)", SD_UPDATE)

    def test_recovery_keeps_only_a_bin_picker(self):
        self.assertIn("loadRecoveryEntries()", SD_UPDATE)
        self.assertIn('FsHelpers::checkFileExtension(std::string_view{name}, ".bin")', SD_UPDATE)
        self.assertNotIn('checkFileExtension(std::string_view{name}, ".elf")', SD_UPDATE)

    def test_bin_files_are_associated_with_sd_firmware_update(self):
        self.assertEqual(MANIFEST["version"], "1.0.1")
        self.assertIn(".bin", MANIFEST["supported_file_types"])

    def test_sd_firmware_bridge_accepts_file_open_handoff(self):
        self.assertIn("NativeFileOpenBridge.h", SD_BRIDGE)
        self.assertIn("NativeFileOpenBridge::activeSourceStoragePath(out)", SD_BRIDGE)
        self.assertIn("activeSourceStoragePath(std::string& out)", FILE_OPEN)


if __name__ == "__main__":
    unittest.main()
