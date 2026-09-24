#!/usr/bin/env python3
"""Focused invariants for independent release index updates."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from update_release_index import update_index  # noqa: E402


SHA = "a" * 64


def package(product, package_id, version, digest=SHA):
    prefix = "app" if product == "apps" else "driver"
    tag = f"{prefix}-{package_id}-v{version}"
    asset = f"{package_id}.elf" if product == "apps" else f"{package_id}--driver.elf"
    if product == "apps":
        manifest = {"file_name": f"{package_id}.elf", "version": version}
    else:
        manifest = {
            "id": package_id, "version": version, "capability": "usb.host", "api": 1,
            "files": [
                {"name": name, "size_bytes": 12, "sha256": digest}
                for name in (".package.json", "driver.elf", "provider-abi.v1", "privileged-imports.v1")
            ],
        }
    return {
        "id": package_id,
        "version": version,
        "tag": tag,
        "asset": asset,
        "url": f"https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/download/{tag}/{asset}",
        "size": 123,
        "sha256": digest,
        "manifest": manifest,
    }


class ReleaseIndexTests(unittest.TestCase):
    def setUp(self):
        self.empty = {"schema": 1, "firmware": None, "apps": [], "drivers": []}

    def test_updates_only_selected_product_and_preserves_others(self):
        index = update_index(self.empty, "apps", package("apps", "clock", "1.0.0"))
        index = update_index(index, "drivers", package("drivers", "usb-host", "0.2.0"))
        index = update_index(index, "apps", package("apps", "clock", "1.1.0"))
        self.assertEqual([e["id"] for e in index["apps"]], ["clock"])
        self.assertEqual(index["apps"][0]["version"], "1.1.0")
        self.assertEqual(index["drivers"][0]["version"], "0.2.0")
        self.assertIsNone(index["firmware"])

    def test_rejects_same_version_with_different_digest(self):
        index = update_index(self.empty, "apps", package("apps", "clock", "1.0.0"))
        with self.assertRaisesRegex(ValueError, "different content"):
            update_index(index, "apps", package("apps", "clock", "1.0.0", "b" * 64))

    def test_rejects_downgrade(self):
        index = update_index(self.empty, "apps", package("apps", "clock", "1.1.0"))
        with self.assertRaisesRegex(ValueError, "move clock backwards"):
            update_index(index, "apps", package("apps", "clock", "1.0.0"))

    def test_rejects_index_larger_than_the_on_device_app_limit(self):
        index = {**self.empty, "apps": [{"id": f"app-{number}"} for number in range(128)]}
        with self.assertRaisesRegex(ValueError, "128-entry limit"):
            update_index(index, "apps", package("apps", "clock", "1.0.0"))

    def test_rejects_cross_release_asset_url(self):
        record = package("drivers", "usb-host", "0.2.0")
        record["url"] = record["url"].replace("driver-usb-host-v0.2.0", "firmware-v9.9.9")
        with self.assertRaisesRegex(ValueError, "immutable GitHub release asset"):
            update_index(self.empty, "drivers", record)


if __name__ == "__main__":
    unittest.main()
