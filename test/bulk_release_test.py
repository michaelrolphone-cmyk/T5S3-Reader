import json
import tempfile
import unittest
from pathlib import Path

from scripts.publish_updated_packages import discover_candidates


class DiscoverCandidatesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "Apps").mkdir()
        (self.root / "Drivers" / "platform_clock_v1").mkdir(parents=True)
        (self.root / "Drivers" / "usb_host_v2").mkdir(parents=True)
        (self.root / "Apps" / "clock.c").write_text("int main(void) { return 0; }")
        (self.root / "Apps" / "reminders.c").write_text("int main(void) { return 0; }")
        (self.root / "Apps" / "clock.json").write_text(
            json.dumps({"file_name": "clock.elf", "version": "1.0.2"}))
        (self.root / "Apps" / "reminders.json").write_text(
            json.dumps({"file_name": "reminders.elf", "version": "1.0.0"}))
        (self.root / "Drivers/platform_clock_v1/manifest.json").write_text(
            json.dumps({"id": "platform-clock-v1", "version": "0.1.0"}))
        (self.root / "Drivers/usb_host_v2/manifest.json").write_text(
            json.dumps({"id": "usb-host-v2", "version": "0.1.4"}))
        (self.root / "platformio.ini").write_text(
            "[riscrte]\nversion = 1.0.1\n", encoding="utf-8")

    def tearDown(self):
        self.temporary.cleanup()

    def current_index(self):
        return {
            "schema": 1,
            "firmware": {"version": "1.0.1"},
            "apps": [
                {"id": "clock", "version": "1.0.1"},
                {"id": "reminders", "version": "1.0.0"},
            ],
            "drivers": [
                {"id": "platform-clock-v1", "version": "0.1.0"},
                {"id": "usb-host-v2", "version": "0.1.3"},
            ],
        }

    def test_selects_only_newer_source_manifest_versions(self):
        self.assertEqual(
            discover_candidates(self.root, self.current_index()),
            [
                {"product": "apps", "id": "clock", "version": "1.0.2"},
                {"product": "drivers", "id": "usb-host-v2", "version": "0.1.4"},
            ],
        )

    def test_includes_firmware_and_orders_it_first_when_newer(self):
        (self.root / "platformio.ini").write_text(
            "[riscrte]\nversion = 1.0.2\n", encoding="utf-8")
        index = self.current_index()
        index["firmware"] = {"version": "1.0.1"}
        candidates = discover_candidates(self.root, index)
        self.assertEqual(candidates[0], {
            "product": "firmware", "id": "", "version": "1.0.2"})
        self.assertEqual(len(candidates), 3)

    def test_numeric_semver_ordering(self):
        (self.root / "Apps" / "clock.json").write_text(
            json.dumps({"file_name": "clock.elf", "version": "1.10.0"}))
        index = self.current_index()
        index["apps"] = [{"id": "clock", "version": "1.9.0"}, {"id": "reminders", "version": "1.0.0"}]
        index["drivers"] = []
        self.assertEqual(
            discover_candidates(self.root, index),
            [
                {"product": "apps", "id": "clock", "version": "1.10.0"},
                {"product": "drivers", "id": "platform-clock-v1", "version": "0.1.0"},
                {"product": "drivers", "id": "usb-host-v2", "version": "0.1.4"},
            ],
        )

    def test_returns_empty_when_everything_matches_latest_release(self):
        index = self.current_index()
        index["drivers"][1]["version"] = "0.1.4"
        index["apps"][0]["version"] = "1.0.2"
        self.assertEqual(discover_candidates(self.root, index), [])

    def test_app_store_version_is_a_normal_release_candidate(self):
        (self.root / "Apps" / "app_store.c").write_text("int main(void) { return 0; }")
        (self.root / "Apps" / "app_store.json").write_text(
            json.dumps({"file_name": "app_store.elf", "version": "1.0.3"}))
        index = self.current_index()
        index["apps"].append({"id": "app_store", "version": "1.0.2"})
        self.assertIn(
            {"product": "apps", "id": "app_store", "version": "1.0.3"},
            discover_candidates(self.root, index),
        )

    def test_ignores_driver_manifests_outside_canonical_release_packages(self):
        legacy = self.root / "Drivers" / "gps_nmea"
        legacy.mkdir()
        (legacy / "manifest.json").write_text(
            json.dumps({"id": "gps-nmea", "version": "2.0.0"}))
        self.assertNotIn(
            {"product": "drivers", "id": "gps-nmea", "version": "2.0.0"},
            discover_candidates(self.root, self.current_index()),
        )


if __name__ == "__main__":
    unittest.main()
