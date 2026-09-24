#!/usr/bin/env python3
"""Validation tests for product-specific release requests and index records."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts.resolve_product_release import resolve
from scripts.build_release_record import build_record


class ProductReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "Apps").mkdir()
        (self.root / "Drivers" / "clock").mkdir(parents=True)

    def tearDown(self):
        self.temp.cleanup()

    def event(self, product, identity, version):
        return {"inputs": {"product": product, "id": identity, "version": version,
                           "commit_firmware": "false"}}

    def test_app_release_uses_app_manifest_version_and_identity(self):
        (self.root / "Apps" / "clock.json").write_text(
            json.dumps({"file_name": "clock.elf", "version": "1.2.3"}))
        result = resolve("workflow_dispatch", self.event("apps", "clock", "1.2.3"), self.root)
        self.assertEqual((result["tag"], result["product"], result["id"]),
                         ("app-clock-v1.2.3", "apps", "clock"))

    def test_driver_release_uses_driver_manifest_version_and_identity(self):
        (self.root / "Drivers" / "clock" / "manifest.json").write_text(
            json.dumps({"id": "platform-clock-v1", "version": "2.0.1"}))
        result = resolve("workflow_dispatch", self.event("drivers", "platform-clock-v1", "2.0.1"), self.root)
        self.assertEqual(result["tag"], "driver-platform-clock-v1-v2.0.1")

    def test_rejects_version_that_differs_from_manifest(self):
        (self.root / "Apps" / "clock.json").write_text(
            json.dumps({"file_name": "clock.elf", "version": "1.2.3"}))
        with self.assertRaisesRegex(ValueError, "manifest version"):
            resolve("workflow_dispatch", self.event("apps", "clock", "1.2.4"), self.root)

    def test_firmware_release_uses_firmware_version_authority(self):
        (self.root / "platformio.ini").write_text("[riscrte]\nversion = 1.2.3\n")
        result = resolve("workflow_dispatch", self.event("firmware", "", "1.2.3"), self.root)
        self.assertEqual(result["tag"], "firmware-v1.2.3")

    def test_driver_record_uses_the_canonical_provider_catalog(self):
        data = b"driver elf"
        packages = self.root / "dist" / "packages"
        packages.mkdir(parents=True)
        release_assets = self.root / "dist" / "release-packages"
        release_assets.mkdir(parents=True)
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
        app_dir = self.root / "dist" / "apps"
        app_dir.mkdir(parents=True)
        (app_dir / "clock.elf").write_bytes(data)
        (app_dir / "clock.json").write_text(json.dumps({"file_name": "clock.elf", "version": "1.2.3"}))
        record = build_record("apps", "clock", "1.2.3", self.root)
        self.assertEqual(record["asset"], "clock.elf")
        self.assertEqual(record["size"], len(data))
        self.assertEqual(record["sha256"], hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    unittest.main()
