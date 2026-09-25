#!/usr/bin/env python3
"""Tests for immutable product release records."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts.build_release_record import build_record


class ProductReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "dist/apps").mkdir(parents=True)
        (self.root / "dist/packages").mkdir(parents=True)
        (self.root / "dist/release-packages").mkdir(parents=True)

    def tearDown(self):
        self.temp.cleanup()

    def test_driver_record_uses_the_canonical_provider_catalog(self):
        data = b"driver elf"
        packages = self.root / "dist" / "packages"
        release_assets = self.root / "dist" / "release-packages"
        (release_assets / "usb-host-v2--driver.elf").write_bytes(data)
        files = []
        for name in (".package.json", "driver.elf", "provider-abi.v1", "privileged-imports.v1"):
            payload = data if name == "driver.elf" else name.encode()
            files.append({"name": name, "size_bytes": len(payload),
                          "sha256": hashlib.sha256(payload).hexdigest()})
        package = {"id": "usb-host-v2", "version": "2.0.1",
                   "capability": "usb.host", "api": 1, "files": files}
        (packages / "usb-provider-catalog.json").write_text(
            json.dumps({"schema": 1, "packages": [package]}))
        record = build_record("drivers", "usb-host-v2", "2.0.1", self.root)
        self.assertEqual(record["asset"], "usb-host-v2--driver.elf")
        self.assertEqual(record["manifest"]["capability"], "usb.host")
        self.assertEqual(record["sha256"], hashlib.sha256(data).hexdigest())

    def test_app_record_hashes_only_the_selected_app_asset(self):
        data = b"clock app image"
        (self.root / "dist/apps/clock.elf").write_bytes(data)
        (self.root / "dist/apps/clock.json").write_text(
            json.dumps({"file_name": "clock.elf", "version": "1.2.3"}))
        record = build_record("apps", "clock", "1.2.3", self.root)
        self.assertEqual(record["asset"], "clock.elf")
        self.assertEqual(record["size"], len(data))
        self.assertEqual(record["sha256"], hashlib.sha256(data).hexdigest())

    def test_firmware_record_uses_configured_version_and_firmware_asset(self):
        data = b"firmware image"
        (self.root / "dist/firmware-t5s3-pro.bin").write_bytes(data)
        (self.root / "platformio.ini").write_text(
            "[riscrte]\nversion = 1.2.3\n", encoding="utf-8")
        record = build_record("firmware", "", "1.2.3", self.root)
        self.assertEqual(record["tag"], "firmware-v1.2.3")
        self.assertEqual(record["asset"], "firmware-t5s3-pro.bin")
        self.assertEqual(record["sha256"], hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    unittest.main()
