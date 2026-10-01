#!/usr/bin/env python3
"""Focused invariants for independent release index updates."""

import copy
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from update_release_index import (BUNDLE_MAX_ARCHIVE_BYTES, serialize_index,
                                  update_index, validate_bundle_manifest,
                                  validate_record)  # noqa: E402


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


def bundle(package_id="usb-host", version="0.3.0", architecture="xtensa-esp32s3"):
    record = package("drivers", package_id, version)
    record["format"] = "rte.zip"
    record["architecture"] = architecture
    record["asset"] = f"driver-{package_id}-{version}-{architecture}.rte.zip"
    record["url"] = ("https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/download/"
                     f"{record['tag']}/{record['asset']}")
    record["size"] = 2048
    record["manifest"] = {
        "schema": 1, "kind": "driver", "id": package_id, "version": version,
        "architecture": architecture, "artifact": "driver.elf", "min_runtime_api": 2,
        "entries": [
            {"name": name, "size_bytes": 96, "sha256": SHA, "executable": name == "driver.elf"}
            for name in ("driver.elf", "provider-abi.v1", "privileged-imports.v1", "readme.txt")
        ],
        "requires": [{"capability": "platform.clock", "min_api": 1}],
    }
    return record


class ReleaseIndexTests(unittest.TestCase):
    def setUp(self):
        self.empty = {"schema": 1, "firmware": None, "apps": [], "drivers": []}

    def test_cdc_lineage_retirement_is_strict_and_preserves_other_rows(self):
        alias = validate_record("drivers", package("drivers", "usb-cdc-acm-v2", "0.1.7"))
        other = validate_record("drivers", package("drivers", "usb-host", "0.1.2"))
        index = {**self.empty, "drivers": [alias, other]}
        for version in ("0.1.6", "0.1.7"):
            with self.assertRaisesRegex(ValueError, "newer than the retired alias"):
                update_index(index, "drivers", bundle("usb-cdc-acm", version))
        updated = update_index(index, "drivers", bundle("usb-cdc-acm", "0.1.8"))
        self.assertEqual([row["id"] for row in updated["drivers"]], ["usb-cdc-acm", "usb-host"])
        self.assertEqual(updated["drivers"][1], other)
        self.assertEqual(index["drivers"], [alias, other])
        with self.assertRaisesRegex(ValueError, "retired CDC alias"):
            update_index(updated, "drivers", bundle("usb-cdc-acm-v2", "0.1.9"))

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

    def test_ota_reader_uses_psram_and_filters_packages(self):
        updater = (Path(__file__).resolve().parents[1] / "src/network/OtaUpdater.cpp").read_text()
        self.assertIn("PsramGrowingTextStream indexJson", updater)
        self.assertIn("PsramJsonAllocator allocator", updater)
        self.assertIn("DeserializationOption::Filter(filter)", updater)
        for field in ("version", "tag", "asset", "url", "size", "sha256"):
            self.assertIn(f'filter["firmware"]["{field}"] = true;', updater)

    def test_index_serialization_has_no_catalog_byte_ceiling(self):
        index = {**self.empty, "apps": [{"id": f"app-{i}", "title": "x" * 500}
                                           for i in range(120)]}
        published = serialize_index(index)
        self.assertEqual(json.loads(published), index)
        index["apps"][0]["title"] = "x" * 16000
        self.assertGreater(len(serialize_index(index).encode()), 65536)

    def test_all_in_repo_apps_have_nonempty_category_arrays(self):
        apps_dir = Path(__file__).resolve().parents[1] / "Apps"
        manifests = sorted(apps_dir.glob("*.json"))
        self.assertGreater(len(manifests), 0)
        for path in manifests:
            manifest = json.loads(path.read_text(encoding="utf-8"))
            categories = manifest.get("category")
            self.assertIsInstance(categories, list, path.name)
            self.assertGreaterEqual(len(categories), 1, path.name)
            self.assertLessEqual(len(categories), 8, path.name)
            self.assertEqual(len(categories), len(set(categories)), path.name)
            for category in categories:
                self.assertIsInstance(category, str, path.name)
                self.assertGreater(len(category), 0, path.name)
                self.assertLess(len(category), 32, path.name)

    def test_accepts_only_the_allowlisted_third_party_gameboy_app(self):
        manifest = {
            "display_name": "GameBoy",
            "file_name": "gameboy.elf",
            "min_firmware_version": "1.2.48",
            "version": "1.2.29",
            "size_bytes": 2107828,
            "sha256": SHA,
            "icon": "solid:f11b",
            "optional": [{"capability": "usb.hid.gamepad", "api": ">=1"}],
        }
        record = {
            "id": "gameboy",
            "version": "1.2.29",
            "tag": "v1.3.1",
            "asset": "gameboy.elf",
            "url": "https://github.com/michaelrolphone-cmyk/T5S3-GameBoy/releases/download/v1.3.1/gameboy.elf",
            "size": 2107828,
            "sha256": SHA,
            "manifest": manifest,
            "source_repo": "michaelrolphone-cmyk/T5S3-GameBoy",
        }
        index = update_index(self.empty, "apps", record)
        self.assertEqual(index["apps"][0]["source_repo"], "michaelrolphone-cmyk/T5S3-GameBoy")
        self.assertEqual(index["apps"][0]["tag"], "v1.3.1")

        record["source_repo"] = "attacker/example"
        with self.assertRaisesRegex(ValueError, "source_repo is reserved"):
            update_index(self.empty, "apps", record)

    def test_firmware_incompatible_app_does_not_invalidate_full_catalog(self):
        host = (Path(__file__).resolve().parents[1] / "src/native/NativeAppHost.cpp").read_text()
        start = host.index("bool loadIndependentAppIndex(")
        end = host.index("\nbool loadAuthoritativeAppCatalog(", start)
        loader = host[start:end]
        self.assertIn("parseAppManifest(asset.manifestJson, asset.manifest, &parsedVersion, true)", loader)
        self.assertNotIn("!asset.manifest.compatible", loader)

    def test_rejects_cross_release_asset_url(self):
        record = package("drivers", "usb-host", "0.2.0")
        record["url"] = record["url"].replace("driver-usb-host-v0.2.0", "firmware-v9.9.9")
        with self.assertRaisesRegex(ValueError, "immutable GitHub release asset"):
            update_index(self.empty, "drivers", record)

    def test_bundle_upgrade_preserves_historical_records_and_other_products(self):
        index = update_index(self.empty, "apps", package("apps", "clock", "1.0.0"))
        index = update_index(index, "drivers", package("drivers", "old-driver", "0.2.0"))
        index = update_index(index, "drivers", package("drivers", "usb-host", "0.2.0"))
        unchanged = copy.deepcopy(index)
        current = bundle()
        updated = update_index(index, "drivers", current)
        self.assertEqual(index, unchanged)
        self.assertEqual(updated["schema"], 1)
        self.assertEqual(updated["apps"], index["apps"])
        self.assertEqual(updated["firmware"], index["firmware"])
        self.assertEqual(updated["drivers"][0], index["drivers"][0])
        self.assertNotIn("format", updated["drivers"][0])
        self.assertEqual(updated["drivers"][1], {**current, "kind": "driver"})
        self.assertEqual(update_index(updated, "drivers", current), updated)
        self.assertEqual(update_index(updated, "drivers", updated["drivers"][0]), updated)

    def test_bundle_conversion_requires_strictly_newer_version(self):
        index = update_index(self.empty, "drivers", package("drivers", "usb-host", "0.3.0"))
        with self.assertRaisesRegex(ValueError, "different content"):
            update_index(index, "drivers", bundle())
        with self.assertRaisesRegex(ValueError, "backwards"):
            update_index(index, "drivers", bundle(version="0.2.0"))

    def test_bundle_same_version_metadata_and_payload_are_immutable(self):
        index = update_index(self.empty, "drivers", bundle())
        candidates = []
        record = bundle()
        record["sha256"] = "b" * 64
        candidates.append(record)
        record = bundle()
        record["manifest"]["requires"][0]["min_api"] = 2
        candidates.append(record)
        candidates.append(bundle(architecture="riscv32"))
        candidates.append(package("drivers", "usb-host", "0.3.0"))
        for record in candidates:
            with self.subTest(record=record), self.assertRaisesRegex(ValueError, "different content"):
                update_index(index, "drivers", record)
        with self.assertRaisesRegex(ValueError, "backwards"):
            update_index(index, "drivers", bundle(version="0.2.0"))

    def test_bundle_record_requires_explicit_consistent_format(self):
        changes = (
            {"format": None}, {"format": "zip"}, {"architecture": "arm64"},
            {"architecture": "riscv32"}, {"asset": "usb-host--driver.elf"},
            {"asset": "driver-usb-host-0.2.0-xtensa-esp32s3.rte.zip"},
            {"url": "https://github.com/other/repo/releases/download/driver-usb-host-v0.3.0/archive.rte.zip"},
            {"kind": "app"}, {"size": True}, {"size": 21},
            {"size": BUNDLE_MAX_ARCHIVE_BYTES + 1}, {"sha256": "A" * 64},
        )
        for change in changes:
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_record("drivers", {**bundle(), **change})
        for field in ("format", "architecture"):
            record = bundle()
            del record[field]
            with self.subTest(missing=field), self.assertRaises(ValueError):
                validate_record("drivers", record)
        for product in ("apps", "firmware"):
            with self.subTest(product=product), self.assertRaises(ValueError):
                validate_record(product, bundle())

    def test_rejects_mixed_legacy_and_ordinary_driver_manifests(self):
        for key, value in (("capability", "usb.host"), ("api", 1), ("files", [])):
            record = bundle()
            record["manifest"][key] = value
            with self.subTest(bundle_key=key), self.assertRaises(ValueError):
                validate_record("drivers", record)
        for key in ("schema", "kind", "architecture", "artifact", "min_runtime_api", "entries"):
            record = package("drivers", "usb-host", "0.3.0")
            record["manifest"][key] = bundle()["manifest"][key]
            with self.subTest(legacy_key=key), self.assertRaisesRegex(ValueError, "mixes"):
                validate_record("drivers", record)
        record = package("drivers", "usb-host", "0.3.0")
        record.update(format="rte.zip", architecture="xtensa-esp32s3")
        with self.assertRaises(ValueError):
            validate_record("drivers", record)

    def test_bundle_manifest_checks_identity_schema_and_runtime_bounds(self):
        invalid = (
            {"schema": True}, {"schema": 3}, {"kind": "provider"}, {"id": "other"},
            {"version": "0.4.0"}, {"architecture": "riscv32"},
            {"artifact": "../driver.elf"}, {"artifact": "DRIVER.elf"},
            {"min_runtime_api": True}, {"min_runtime_api": 0},
            {"min_runtime_api": 0x100000000}, {"unknown": "value"},
        )
        for change in invalid:
            record = bundle()
            record["manifest"].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_record("drivers", record)
        for identity, version in (("bad.id", "0.3.0"), ("bad-", "0.3.0"),
                                  ("a" * 64, "0.3.0"), ("driver", "01.0.0"),
                                  ("driver", "4294967296.0.0")):
            with self.subTest(identity=identity, version=version), self.assertRaises(ValueError):
                validate_record("drivers", bundle(identity, version))
        record = bundle(architecture="riscv32")
        self.assertEqual(validate_record("drivers", record)["architecture"], "riscv32")

    def test_bundle_manifest_rejects_unsafe_inventory_names_and_roles(self):
        for name in ("../resource.txt", "nested/resource.txt", "C:\\resource.txt", ".package.json",
                     "Readme.txt", "readme.", "read..me", "_readme", "a" * 128, "driver.elf"):
            record = bundle()
            record["manifest"]["entries"][3]["name"] = name
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_record("drivers", record)
        mutations = (
            {"executable": 1}, {"executable": True}, {"name": "hidden.elf"},
            {"size_bytes": True}, {"size_bytes": 0}, {"size_bytes": 1024 * 1024 + 1},
            {"sha256": "not-a-sha"}, {"role": "resource"},
        )
        for change in mutations:
            record = bundle()
            record["manifest"]["entries"][3].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_record("drivers", record)
        for field, value in (("size_bytes", 51), ("executable", False)):
            record = bundle()
            record["manifest"]["entries"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_record("drivers", record)
        for index in range(3):
            record = bundle()
            del record["manifest"]["entries"][index]
            with self.subTest(missing_entry=index), self.assertRaises(ValueError):
                validate_record("drivers", record)

    def test_bundle_manifest_enforces_inventory_and_manifest_budgets(self):
        record = bundle()
        record["manifest"]["entries"] = []
        with self.assertRaises(ValueError):
            validate_record("drivers", record)
        record = bundle()
        prototype = record["manifest"]["entries"].pop()
        record["manifest"]["entries"].extend(
            {**prototype, "name": f"resource-{number}.txt"} for number in range(14))
        with self.assertRaisesRegex(ValueError, "1-16 entries"):
            validate_record("drivers", record)
        record = bundle()
        for entry in record["manifest"]["entries"]:
            entry["size_bytes"] = 1024 * 1024
        with self.assertRaisesRegex(ValueError, "total-content"):
            validate_record("drivers", record)
        record = bundle()
        prototype = record["manifest"]["entries"].pop()
        record["manifest"]["entries"].extend(
            {**prototype, "name": f"r{number}" + "x" * 120} for number in range(13))
        record["manifest"]["requires"] = [
            {"capability": "c" + str(number) + "x" * 58, "min_api": 1} for number in range(16)]
        with self.assertRaisesRegex(ValueError, "4096-byte"):
            validate_record("drivers", record)

    def test_bundle_manifest_enforces_bounded_unique_dependencies(self):
        for requires in ({}, [None], [{"capability": "clock", "api": 1}],
                         [{"capability": "clock", "min_api": True}],
                         [{"capability": "clock", "min_api": 0x100000000}],
                         [{"capability": "unsafe..clock", "min_api": 1}],
                         [{"capability": "clock", "min_api": 1}] * 2,
                         [{"capability": f"clock{number}", "min_api": 1} for number in range(17)]):
            record = bundle()
            record["manifest"]["requires"] = requires
            with self.subTest(requires=requires), self.assertRaises(ValueError):
                validate_record("drivers", record)
        for count in (0, 16):
            record = bundle()
            record["manifest"]["requires"] = [
                {"capability": f"clock{number}", "min_api": 1} for number in range(count)]
            manifest = record["manifest"]
            self.assertIs(validate_bundle_manifest(manifest, record["id"], record["version"],
                                                    record["architecture"]), manifest)


if __name__ == "__main__":
    unittest.main()
