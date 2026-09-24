import json
import tempfile
import unittest
from pathlib import Path

from scripts.publish_updated_packages import discover_candidates


class DiscoverCandidatesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "dist/apps").mkdir(parents=True)
        (self.root / "dist/packages").mkdir(parents=True)
        (self.root / "dist/apps/app-catalog.json").write_text(
            json.dumps({
                "schema": 1,
                "apps": [
                    {"file_name": "clock.elf", "version": "1.0.2"},
                    {"file_name": "reminders.elf", "version": "1.0.0"},
                ],
            }),
            encoding="utf-8",
        )
        (self.root / "dist/packages/usb-provider-catalog.json").write_text(
            json.dumps({
                "schema": 1,
                "packages": [
                    {"id": "usb-host", "version": "0.2.0"},
                    {"id": "new-provider", "version": "1.0.0"},
                    {"id": "older-provider", "version": "0.9.9"},
                ],
            }),
            encoding="utf-8",
        )

    def tearDown(self):
        self.temporary.cleanup()

    def test_selects_only_newer_and_unreleased_packages(self):
        index = {
            "schema": 1,
            "firmware": None,
            "apps": [
                {"id": "clock", "version": "1.0.1"},
                {"id": "reminders", "version": "1.0.0"},
            ],
            "drivers": [
                {"id": "usb-host", "version": "0.2.0"},
                {"id": "older-provider", "version": "1.0.0"},
            ],
        }

        self.assertEqual(
            discover_candidates(self.root, index),
            [
                {"product": "apps", "id": "clock", "version": "1.0.2"},
                {"product": "drivers", "id": "new-provider", "version": "1.0.0"},
            ],
        )

    def test_numeric_semver_ordering(self):
        (self.root / "dist/apps/app-catalog.json").write_text(
            json.dumps({
                "schema": 1,
                "apps": [{"file_name": "clock.elf", "version": "1.10.0"}],
            }),
            encoding="utf-8",
        )
        (self.root / "dist/packages/usb-provider-catalog.json").write_text(
            json.dumps({"schema": 1, "packages": []}),
            encoding="utf-8",
        )
        index = {
            "schema": 1,
            "firmware": None,
            "apps": [{"id": "clock", "version": "1.9.0"}],
            "drivers": [],
        }

        self.assertEqual(
            discover_candidates(self.root, index),
            [{"product": "apps", "id": "clock", "version": "1.10.0"}],
        )

    def test_returns_empty_when_everything_matches_latest_release(self):
        (self.root / "dist/packages/usb-provider-catalog.json").write_text(
            json.dumps({
                "schema": 1,
                "packages": [
                    {"id": "usb-host", "version": "0.2.0"},
                    {"id": "new-provider", "version": "1.0.0"},
                    {"id": "older-provider", "version": "0.9.9"},
                ],
            }),
            encoding="utf-8",
        )
        index = {
            "schema": 1,
            "firmware": None,
            "apps": [
                {"id": "clock", "version": "1.0.2"},
                {"id": "reminders", "version": "1.0.0"},
            ],
            "drivers": [
                {"id": "usb-host", "version": "0.2.0"},
                {"id": "new-provider", "version": "1.0.0"},
                {"id": "older-provider", "version": "0.9.9"},
            ],
        }
        (self.root / "dist/apps/app-catalog.json").write_text(
            json.dumps({
                "schema": 1,
                "apps": [
                    {"file_name": "clock.elf", "version": "1.0.2"},
                    {"file_name": "reminders.elf", "version": "1.0.0"},
                ],
            }),
            encoding="utf-8",
        )

        self.assertEqual(discover_candidates(self.root, index), [])


if __name__ == "__main__":
    unittest.main()
