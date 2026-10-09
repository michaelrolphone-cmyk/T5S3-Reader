import json
import runpy
import types
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "test"))
import product_release_test as fixtures
from scripts.publish_updated_packages import discover_candidates, release_assets  # noqa: E402
from scripts import build_release_candidates as release_builder  # noqa: E402
from scripts.build_release_candidates import discover_driver_builders  # noqa: E402


class DiscoverCandidatesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "Apps").mkdir()
        (self.root / "scripts").mkdir()
        (self.root / "Apps" / "clock.c").write_text("int main(void) { return 0; }")
        (self.root / "Apps" / "reminders.c").write_text("int main(void) { return 0; }")
        (self.root / "Apps" / "clock.json").write_text(
            json.dumps({"file_name": "clock.elf", "version": "1.0.2"}))
        (self.root / "Apps" / "reminders.json").write_text(
            json.dumps({"file_name": "reminders.elf", "version": "1.0.0"}))
        self.provider("platform_clock_v1", "platform-clock-v1", "0.1.0")
        self.provider("usb_host_v2", "usb-host-v2", "0.1.4")
        (self.root / "platformio.ini").write_text(
            "[riscrte]\nversion = 1.0.1\n", encoding="utf-8")

    def provider(self, folder, identity, version="1.0.0", abi=2):
        source = self.root / "Drivers" / folder
        source.mkdir(parents=True)
        manifest = {"type": "driver", "id": identity, "version": version,
                    "driver_abi": abi, "architecture": "xtensa-esp32s3",
                    "file_name": "driver.elf", "requires": [],
                    "provides": [{"capability": "test.provider", "api": 1}]}
        (source / "manifest.json").write_text(json.dumps(manifest))
        (self.root / "scripts" / f"build_{folder}.py").write_text("# fixture builder\n")
        return source

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

    def test_app_release_is_one_exact_current_bundle(self):
        app = fixtures.stage_app(self.root, version='1.0.2')
        # Existing unrelated loose assets/catalogs never become publication assets.
        (self.root / 'dist/apps').mkdir(exist_ok=True)
        (self.root / 'dist/apps/clock.elf').write_bytes(b'legacy')
        stale = app['output'] / 'application-clock-1.0.1-xtensa-esp32s3.rte.zip'
        stale.write_bytes(b'stale')
        self.assertEqual(release_assets(self.root, 'apps', 'clock', '1.0.2'),
                         [app['output'] / app['name']])
        with self.assertRaisesRegex(ValueError, 'stale'):
            release_assets(self.root, 'apps', 'clock', '1.0.1')

    def test_selected_apps_share_one_isolated_export(self):
        candidates = [{'product': 'apps', 'id': 'clock', 'version': '1.0.2'},
                      {'product': 'apps', 'id': 'reminders', 'version': '1.0.0'}]
        with patch.object(release_builder, 'run') as run:
            release_builder.build_apps(candidates)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual(commands[:2], [[sys.executable, 'scripts/build_all_apps.py', '--id', 'clock'],
                                      [sys.executable, 'scripts/build_all_apps.py', '--id', 'reminders']])
        self.assertEqual(commands[2], [sys.executable, 'scripts/export_canonical_driver_release.py',
            '--output', 'dist/release-app-packages', '--kind', 'application', '--ids', 'clock', 'reminders'])

    def test_new_module_kinds_use_existing_selective_build_and_isolated_assets(self):
        for kind in ('service', 'provider'):
            identity = kind + '-fixture'
            staged = fixtures.stage_module(self.root, kind, identity)
            (self.root / 'scripts' / f"build_{identity.replace('-', '_')}.py").write_text('# fixture builder')
            candidate = {'product': kind + 's', 'id': identity, 'version': '1.0.0'}
            self.assertIn(candidate, discover_candidates(self.root, self.current_index()))
            self.assertEqual(release_assets(self.root, kind + 's', identity, '1.0.0'),
                             [staged['output'] / staged['name']])
            with patch.object(release_builder, 'ROOT', self.root), patch.object(release_builder, 'run') as run:
                release_builder.build_drivers([candidate], kind)
            commands = [call.args[0] for call in run.call_args_list]
            self.assertEqual(commands[0][-1], f"scripts/build_{identity.replace('-', '_')}.py")
            self.assertEqual(commands[-1], [sys.executable, 'scripts/export_canonical_driver_release.py',
                '--output', f'dist/release-{kind}-packages', '--kind', kind, '--ids', identity])

    def test_ambiguous_flat_output_is_rejected_before_any_builder(self):
        fixtures.stage_module(self.root, 'service', 'clock')
        (self.root / 'scripts/build_clock.py').write_text('# fixture builder')
        with patch.object(release_builder, 'ROOT', self.root), patch.object(release_builder, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'collides'):
                release_builder.build_plan([{'product': 'services', 'id': 'clock', 'version': '1.0.0'}])
            run.assert_not_called()

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

    def test_usb_mass_storage_is_planned_and_selectively_buildable(self):
        self.provider("usb_mass_storage", "usb-mass-storage", "0.1.1")
        self.assertIn(
            {"product": "drivers", "id": "usb-mass-storage", "version": "0.1.1"},
            discover_candidates(self.root, self.current_index()),
        )
        builders = discover_driver_builders(self.root)
        self.assertIn(("scripts/build_usb_mass_storage.py",), builders["usb-mass-storage"])

    def test_usb_hid_text_input_is_planned_and_selectively_buildable(self):
        self.provider("usb_hid_text_input", "usb-hid-text-input", "0.1.0")
        self.assertIn(
            {"product": "drivers", "id": "usb-hid-text-input", "version": "0.1.0"},
            discover_candidates(self.root, self.current_index()),
        )
        builders = discover_driver_builders(self.root)
        self.assertIn(
            ("scripts/build_usb_hid_text_input.py",),
            builders["usb-hid-text-input"],
        )

    def test_release_builder_covers_every_packaged_driver(self):
        # Derive coverage independently from every current ABI-2 source;
        # a newly added provider must not need a second hard-coded ID table.
        packaged = set()
        for path in (ROOT / "Drivers").glob("*/manifest.json"):
            manifest = json.loads(path.read_text())
            if manifest.get("type") == "driver" and manifest.get("driver_abi") == 2:
                packaged.add(manifest["id"])
        self.assertTrue(packaged)
        self.assertEqual(packaged, set(discover_driver_builders(ROOT)))

    def test_x4_driver_release_builds_only_the_selected_source(self):
        builders = discover_driver_builders(ROOT)
        self.assertEqual(builders["x4pro-panel"],
                         [("scripts/build_x4pro_drivers.py", "--source", "x4pro_panel")])
        self.assertEqual(builders["x4pro-sd"],
                         [("scripts/build_x4pro_drivers.py", "--source", "x4pro_sd")])

    def test_ignores_driver_manifests_outside_canonical_release_packages(self):
        self.provider("gps_nmea", "gps-nmea", "2.0.0", abi=1)
        self.assertNotIn(
            {"product": "drivers", "id": "gps-nmea", "version": "2.0.0"},
            discover_candidates(self.root, self.current_index()),
        )

    def test_new_provider_is_planned_and_built_without_an_allowlist(self):
        self.provider("extra_sensor", "independent-sensor", "2.1.0")
        expected = {"product": "drivers", "id": "independent-sensor", "version": "2.1.0"}
        self.assertIn(expected, discover_candidates(self.root, self.current_index()))
        with patch.object(release_builder, "ROOT", self.root), \
                patch.object(release_builder, "run") as run:
            release_builder.build_drivers([expected])
        self.assertEqual(run.call_args_list, [
            unittest.mock.call([sys.executable, "scripts/build_extra_sensor.py"]),
            unittest.mock.call([sys.executable, "scripts/build_installed_usb_stack.py",
                                "--ids", "independent-sensor"]),
            unittest.mock.call([sys.executable, "test/drivers/installed_usb_stack_package_test.py",
                                "--ids", "independent-sensor"]),
            unittest.mock.call([sys.executable, "scripts/export_canonical_driver_release.py",
                                "--output", "dist/release-packages", "--kind", "driver", "--ids", "independent-sensor"]),
        ])

    def test_unknown_or_duplicate_selection_fails_before_building(self):
        for identities in (["usb-host-v2", "absent"], ["usb-host-v2", "usb-host-v2"]):
            with self.subTest(identities=identities), \
                    patch.object(release_builder, "ROOT", self.root), \
                    patch.object(release_builder, "run") as run:
                with self.assertRaises(ValueError):
                    release_builder.build_drivers([{"id": identity} for identity in identities])
                run.assert_not_called()

    def test_missing_unsafe_duplicate_and_invalid_source_fail_closed(self):
        script = self.root / "scripts/build_usb_host_v2.py"
        script.unlink()
        with self.assertRaises(ValueError):
            discover_driver_builders(self.root)
        script.symlink_to(self.root / "scripts/build_platform_clock_v1.py")
        with self.assertRaises(ValueError):
            discover_driver_builders(self.root)
        script.unlink()
        script.write_text("# fixture builder\n")
        duplicate = self.provider("duplicate", "usb-host-v2")
        with self.assertRaises(ValueError):
            discover_driver_builders(self.root)
        (duplicate / "manifest.json").unlink()
        self.provider("invalid", "bad-version", "legacy")
        with self.assertRaises(ValueError):
            discover_driver_builders(self.root)

    def test_physical_probes_keep_their_strict_audits(self):
        builders = discover_driver_builders(ROOT)
        physical_probes = {"i2c_esp32s3_v2", "usb_controller_esp32s3"}
        self.assertTrue(physical_probes <= release_builder._BOOTSTRAP_BUILD_RECIPES.keys())
        for folder, commands in release_builder._BOOTSTRAP_BUILD_RECIPES.items():
            manifest = json.loads((ROOT / "Drivers" / folder / "manifest.json").read_text())
            self.assertEqual(builders[manifest["id"]], commands)
            if folder in physical_probes:
                self.assertIn("--strict", commands[-1])

    def test_power_wrappers_build_only_their_own_manifest_identity(self):
        stub = types.ModuleType("build_board_power_t5s3_v2")
        stub.build = unittest.mock.Mock()
        for folder in ("bq25896", "t5s3_usb_power_profile"):
            with self.subTest(folder=folder), patch.dict(sys.modules,
                    {"build_board_power_t5s3_v2": stub}):
                stub.build.reset_mock()
                runpy.run_path(str(ROOT / "scripts" / f"build_{folder}.py"), run_name="__main__")
                manifest = json.loads((ROOT / "Drivers" / folder / "manifest.json").read_text())
                stub.build.assert_called_once_with(identities=[manifest["id"]])

    def test_driver_release_selects_only_exact_current_archive(self):
        output = self.root / "dist/release-packages"
        output.mkdir(parents=True)
        expected = output / "driver-usb-host-v2-0.1.4-xtensa-esp32s3.rte.zip"
        expected.write_bytes(b"current archive")
        for name in (
                "driver-usb-host-v2-extra-0.1.4-xtensa-esp32s3.rte.zip",
                "driver-usb-host-v2-0.1.3-xtensa-esp32s3.rte.zip",
                "driver-usb-host-v2-0.1.4-riscv32.rte.zip",
                "usb-host-v2--driver.elf", "usb-host-v2--manifest.json"):
            (output / name).write_bytes(b"unrelated or obsolete asset")
        self.assertEqual(release_assets(self.root, "drivers", "usb-host-v2", "0.1.4"),
                         [expected])
        with self.assertRaisesRegex(ValueError, "source version changed"):
            release_assets(self.root, "drivers", "usb-host-v2", "0.1.3")
        expected.unlink()
        with self.assertRaisesRegex(ValueError, "missing or empty assets"):
            release_assets(self.root, "drivers", "usb-host-v2", "0.1.4")

    def test_driver_release_rejects_empty_and_symlink_archives(self):
        output = self.root / "dist/release-packages"
        output.mkdir(parents=True)
        expected = output / "driver-usb-host-v2-0.1.4-xtensa-esp32s3.rte.zip"
        expected.touch()
        with self.assertRaisesRegex(ValueError, "missing or empty assets"):
            release_assets(self.root, "drivers", "usb-host-v2", "0.1.4")
        expected.unlink()
        neighbor = output / "unrelated.rte.zip"
        neighbor.write_bytes(b"unrelated archive")
        expected.symlink_to(neighbor)
        with self.assertRaisesRegex(ValueError, "symlink"):
            release_assets(self.root, "drivers", "usb-host-v2", "0.1.4")


if __name__ == "__main__":
    unittest.main()
