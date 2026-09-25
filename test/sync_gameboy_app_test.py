#!/usr/bin/env python3
"""Tests for the temporary GameBoy app-store provider integration."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from sync_gameboy_app import record_from_release, upsert_gameboy_app  # noqa: E402


SHA = "a" * 64
MANIFEST = {
    "display_name": "GameBoy",
    "file_name": "gameboy.elf",
    "min_firmware_version": "1.2.48",
    "version": "1.2.29",
    "size_bytes": 2107828,
    "sha256": SHA,
    "icon": "solid:f11b",
    "optional": [{"capability": "usb.hid.gamepad", "api": ">=1"}],
}
RELEASE = {
    "tag_name": "v1.3.1",
    "draft": False,
    "prerelease": False,
    "assets": [
        {"name": "gameboy.elf", "size": 2107828, "digest": "sha256:" + SHA,
         "browser_download_url": "https://github.com/michaelrolphone-cmyk/T5S3-GameBoy/releases/download/v1.3.1/gameboy.elf"},
        {"name": "gameboy.json", "size": 551,
         "browser_download_url": "https://github.com/michaelrolphone-cmyk/T5S3-GameBoy/releases/download/v1.3.1/gameboy.json"},
    ],
}


class GameBoyAppIndexTests(unittest.TestCase):
    def setUp(self):
        self.empty = {"schema": 1, "firmware": None, "apps": [], "drivers": []}

    def test_builds_an_index_record_pointing_to_the_upstream_release(self):
        record = record_from_release(RELEASE, MANIFEST)
        self.assertEqual(record["id"], "gameboy")
        self.assertEqual(record["version"], "1.2.29")
        self.assertEqual(record["source_repo"], "michaelrolphone-cmyk/T5S3-GameBoy")
        index, changed = upsert_gameboy_app(self.empty, record)
        self.assertTrue(changed)
        self.assertEqual(index["apps"][0]["url"], RELEASE["assets"][0]["browser_download_url"])

    def test_rejects_a_release_asset_from_an_untrusted_host_or_repo(self):
        release = {**RELEASE, "assets": [dict(item) for item in RELEASE["assets"]]}
        release["assets"][0]["browser_download_url"] = "https://example.com/gameboy.elf"
        with self.assertRaisesRegex(ValueError, "asset URL is invalid"):
            record_from_release(release, MANIFEST)

    def test_rejects_api_asset_digest_disagreement(self):
        release = {**RELEASE, "assets": [dict(item) for item in RELEASE["assets"]]}
        release["assets"][0]["digest"] = "sha256:" + "b" * 64
        with self.assertRaisesRegex(ValueError, "digest does not match"):
            record_from_release(release, MANIFEST)

    def test_rejects_unpublished_prereleases(self):
        release = {**RELEASE, "prerelease": True}
        with self.assertRaisesRegex(ValueError, "published stable release"):
            record_from_release(release, MANIFEST)

    def test_does_not_allow_manifest_changes_without_app_version_bump(self):
        record = record_from_release(RELEASE, MANIFEST)
        index, _ = upsert_gameboy_app(self.empty, record)
        changed_manifest = {**MANIFEST, "icon": "solid:f11c"}
        changed_record = record_from_release(RELEASE, changed_manifest)
        with self.assertRaisesRegex(ValueError, "without an app manifest version bump"):
            upsert_gameboy_app(index, changed_record)

    def test_index_update_preserves_other_products_and_rejects_downgrade(self):
        record = record_from_release(RELEASE, MANIFEST)
        existing = {**self.empty, "firmware": {"version": "1.2.70"},
                    "apps": [{"id": "clock", "version": "1.0.0"}]}
        index, _ = upsert_gameboy_app(existing, record)
        self.assertEqual(index["firmware"]["version"], "1.2.70")
        self.assertEqual({item["id"] for item in index["apps"]}, {"clock", "gameboy"})
        older = {**record, "version": "1.2.28",
                 "manifest": {**MANIFEST, "version": "1.2.28"}}
        with self.assertRaisesRegex(ValueError, "backwards"):
            upsert_gameboy_app(index, older)


if __name__ == "__main__":
    unittest.main()
