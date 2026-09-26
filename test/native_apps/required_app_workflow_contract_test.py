#!/usr/bin/env python3
"""Required-app workflows install in place and repeat the original launch step."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SETTINGS = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text(encoding="utf-8")
BATTERY = (ROOT / "src/activities/settings/BatteryStatusActivity.cpp").read_text(encoding="utf-8")
REQUIRED = (ROOT / "src/activities/util/RequiredAppActivity.cpp").read_text(encoding="utf-8")
HOST = (ROOT / "src/native/NativeAppHost.cpp").read_text(encoding="utf-8")
HOST_HEADER = (ROOT / "src/native/NativeAppHost.h").read_text(encoding="utf-8")


class RequiredAppWorkflowContract(unittest.TestCase):
    def test_settings_resolves_managed_install_and_has_no_hardcoded_loose_path(self):
        self.assertIn('resolveInstalledAppPath("settings.elf", settingsPath)', SETTINGS)
        self.assertIn("runNativeApp(settingsPath.c_str()", SETTINGS)
        self.assertNotIn('runNativeApp("/sd/Apps/settings.elf"', SETTINGS)

    def test_missing_settings_stays_inside_parent_workflow(self):
        self.assertIn("std::make_unique<RequiredAppActivity>", SETTINGS)
        self.assertIn('"settings.elf", "Settings"', SETTINGS)
        self.assertIn("launchAttempted = false;", SETTINGS)
        self.assertIn("launchFailed = false;", SETTINGS)

    def test_battery_settings_action_uses_required_app_install_and_resumes(self):
        self.assertIn('resolveInstalledAppPath("battery.elf", batteryPath)', BATTERY)
        self.assertIn('std::make_unique<RequiredAppActivity>', BATTERY)
        self.assertIn('"battery.elf", "Battery Status"', BATTERY)
        self.assertIn("launchAttempted = false;", BATTERY)
        self.assertNotIn('runNativeApp("/sd/Apps/battery.elf"', BATTERY)
        self.assertNotIn("Install Battery Status from the App Store", BATTERY)

    def test_required_app_screen_has_inline_install_and_retry(self):
        self.assertIn('mappedInput.mapLabels("Back"', REQUIRED)
        self.assertIn('"Install"', REQUIRED)
        self.assertIn('"Retry"', REQUIRED)
        self.assertIn("installRequiredNativeApp(", REQUIRED)
        self.assertIn("requestUpdateAndWait();", REQUIRED)
        self.assertIn("result.isCancelled = false;", REQUIRED)

    def test_required_install_uses_authoritative_catalog_and_verifies_publication(self):
        self.assertIn("installRequiredNativeApp", HOST_HEADER)
        self.assertIn("loadAvailableAppCatalog(catalog)", HOST)
        self.assertIn("RuntimeOnlinePackages::installApplication(", HOST)
        self.assertIn("resolveInstalledAppPath(artifact, installedPath, &installedManifest)", HOST)
        self.assertNotIn("bool connectSavedWifi()", HOST)
        self.assertIn("HttpDownloader::fetchUrl", HOST)

    def test_app_store_and_required_flow_share_catalog_fallback(self):
        self.assertIn("bool loadAvailableAppCatalog(", HOST)
        self.assertIn("return loadAvailableAppCatalog(s->catalog);", HOST)
        self.assertIn("loadAggregateCatalog(releaseAssets, catalog, catalogUrl)", HOST)
        self.assertIn("loadCatalogManifests(releaseAssets, catalog)", HOST)


if __name__ == "__main__":
    unittest.main()
