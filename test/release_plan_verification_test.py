#!/usr/bin/env python3
"""Exercise the offline pre-upload/post-restore release gate, without publishing."""
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'test'))
import product_release_test as fixtures
from scripts.verify_release_plan import validate_plan, verify_plan


class ReleasePlanVerificationTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.ProductReleaseTests()
        self.fixture.setUp()
        self.root = self.fixture.root
        self.plan = [{'product': 'drivers', 'id': 'test-driver', 'version': '2.0.1'}]

    def tearDown(self):
        self.fixture.tearDown()

    def test_four_kind_restore_and_new_kind_corruption(self):
        self.add_app()
        artifacts = []
        for kind in ('service', 'provider'):
            staged = fixtures.stage_module(self.root, kind, kind + '-fixture')
            self.plan.append({'product': kind + 's', 'id': kind + '-fixture', 'version': '1.0.0'})
            artifacts.append(staged['output'] / staged['name'])
        # Restored ZIPs/catalogs are sufficient; no hidden build staging is needed.
        shutil.rmtree(self.root / 'dist/packages')
        self.assertEqual(len(verify_plan(self.root, self.plan)), 4)
        for path in artifacts:
            original = path.read_bytes()
            path.write_bytes(original[:-1])
            with self.assertRaises(ValueError):
                verify_plan(self.root, self.plan)
            path.write_bytes(original)

    def add_app(self):
        self.app = fixtures.stage_app(self.root)
        self.plan.append({'product': 'apps', 'id': 'clock', 'version': '1.2.3'})

    def repack_app(self):
        stage = self.app['stage']
        manifest = self.app['manifest']
        for entry in manifest['entries']:
            data = (stage / entry['name']).read_bytes()
            entry['size_bytes'] = len(data)
            entry['sha256'] = hashlib.sha256(data).hexdigest()
        (stage / '.package.json').write_text(json.dumps(manifest, separators=(',', ':')))
        archive = fixtures.pack_directory(stage)
        (self.app['output'] / self.app['name']).write_bytes(archive)
        (self.app['output'] / 'package-catalog.json').write_text(json.dumps({
            'schema': 1, 'release': 'unpublished-build',
            'packages': [fixtures.catalog_row(stage, self.app['name'], archive)]}))

    def add_firmware(self):
        version, payload = '1.2.3', b'firmware app image'
        (self.root / 'platformio.ini').write_text('[riscrte]\nversion = ' + version + '\n')
        (self.root / 'firmware').mkdir()
        (self.root / 'dist/firmware-t5s3-pro.bin').write_bytes(payload)
        (self.root / f'dist/riscrte_lilygo_t5s3_{version}-app.bin').write_bytes(payload)
        (self.root / f'dist/riscrte_lilygo_t5s3_{version}.elf').write_bytes(b'debug symbols')
        (self.root / f'firmware/riscrte_lilygo_t5s3_{version}.bin').write_bytes(b'merged full flash')
        self.plan.append({'product': 'firmware', 'id': '', 'version': version})

    def test_mixed_plan_and_exact_product_filter(self):
        self.add_app(); self.add_firmware()
        self.assertEqual(len(verify_plan(self.root, self.plan)), 3)
        self.assertEqual(verify_plan(self.root, self.plan, 'drivers')[0]['format'], 'rte.zip')
        self.assertEqual(len(verify_plan(self.root, self.plan, 'apps')), 1)
        self.assertEqual(verify_plan(self.root, []), [])

    def test_driver_handoff_needs_only_exported_bundle_catalog_and_source(self):
        shutil.rmtree(self.root / 'dist/packages')
        self.assertEqual(len(verify_plan(self.root, self.plan)), 1)

    def test_mixed_restore_needs_no_loose_or_hidden_intermediates(self):
        self.add_app()
        shutil.rmtree(self.root / 'dist/packages')
        shutil.rmtree(self.root / 'dist/apps')
        records = verify_plan(self.root, self.plan)
        self.assertEqual(len(records), 2)
        self.assertTrue(all(record['format'] == 'rte.zip' for record in records))

    def test_missing_restored_driver_archive_fails_before_publication(self):
        self.fixture.archive_path.unlink()
        with self.assertRaises(ValueError):
            verify_plan(self.root, self.plan)

    def test_mutated_restore_or_catalog_fails(self):
        self.fixture.archive_path.write_bytes(b'changed during handoff')
        with self.assertRaises(ValueError):
            verify_plan(self.root, self.plan)

    def test_bundled_app_sidecar_integrity_must_match_elf(self):
        self.add_app()
        (self.app['stage'] / 'clock.elf').write_bytes(b'changed payload'.ljust(96, b'!'))
        self.repack_app()
        with self.assertRaisesRegex(ValueError, 'sidecar differs'):
            verify_plan(self.root, self.plan)

    def test_bundled_app_sidecar_keeps_runtime_size_bound(self):
        self.add_app()
        path = self.app['stage'] / 'clock.json'
        path.write_text(path.read_text() + ' ' * 2048)
        self.repack_app()
        with self.assertRaisesRegex(ValueError, 'oversized'):
            verify_plan(self.root, self.plan)

    def test_firmware_alias_and_planned_version_guard(self):
        self.add_firmware()
        alias = self.root / 'dist/riscrte_lilygo_t5s3_1.2.3-app.bin'
        alias.write_bytes(b'wrong alias')
        with self.assertRaisesRegex(ValueError, 'OTA aliases'):
            verify_plan(self.root, self.plan)
        alias.write_bytes((self.root / 'dist/firmware-t5s3-pro.bin').read_bytes())
        self.plan[-1]['version'] = '1.2.4'
        with self.assertRaisesRegex(ValueError, 'source version'):
            verify_plan(self.root, self.plan)

    def test_symlinked_restored_app_is_rejected(self):
        self.add_app()
        path = self.app['output'] / self.app['name']
        target = self.root / 'real.rte.zip'
        path.rename(target); path.symlink_to(target)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            verify_plan(self.root, self.plan)

    def test_plan_validation_happens_before_product_filter(self):
        for bad in [[*self.plan, self.plan[0]], [{'product': 'unknown', 'id': 'x', 'version': '1.0.0'}],
                    [{'product': 'apps', 'id': '../escape', 'version': '1.0.0'}],
                    [{'product': 'firmware', 'id': 'wrong', 'version': '1.0.0'}],
                    [{'product': 'apps', 'id': 'x', 'version': '01.0.0'}]]:
            with self.subTest(plan=bad), self.assertRaises(ValueError):
                verify_plan(self.root, bad, 'drivers')

    def test_offline_cli_changes_no_index_or_artifacts(self):
        plan = self.root / 'release-plan.json'
        plan.write_text(json.dumps(self.plan))
        before = self.fixture.archive_path.read_bytes()
        completed = subprocess.run([sys.executable, str(ROOT / 'scripts/verify_release_plan.py'),
                                    '--plan', str(plan), '--root', str(self.root)],
                                   check=True, capture_output=True, text=True)
        self.assertIn('no publication or index changes', completed.stdout)
        self.assertEqual(self.fixture.archive_path.read_bytes(), before)
        self.assertFalse((self.root / 'release-index.json').exists())


if __name__ == '__main__':
    unittest.main()
