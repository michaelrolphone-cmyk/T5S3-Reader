#!/usr/bin/env python3
"""Round-trip and corruption checks for immutable bundled driver records."""
import copy
import hashlib
import json
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.build_release_record import build_record
from scripts.pack_rte_zip import pack_directory, catalog_row, crc32, put_local, put_central
from scripts.update_release_index import update_index, validate_record, serialize_index


def stage_app(root, identity='clock', version='1.2.3', requires=None):
    source = {'file_name': identity+'.elf', 'version': version,
              'display_name': 'Fixture app', 'min_firmware_version': '1.2.0',
              'icon': 'solid:f017'}
    if requires is not None:
        source['requires'] = requires
    app_source = root / 'Apps' / (identity+'.json')
    app_source.parent.mkdir(exist_ok=True)
    app_source.write_text(json.dumps(source))
    app_source.with_suffix('.c').write_text('/* fixture source */')
    payload = b'fixture application ELF'.ljust(96, b'\x00')
    sidecar = dict(source, size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    stage = root / 'dist/packages' / identity
    stage.mkdir(parents=True, exist_ok=True)
    (stage / (identity+'.elf')).write_bytes(payload)
    (stage / (identity+'.json')).write_text(json.dumps(sidecar, separators=(',', ':')))
    entries = []
    for name in (identity+'.elf', identity+'.json'):
        data = (stage / name).read_bytes()
        entries.append({'name': name, 'size_bytes': len(data),
                        'sha256': hashlib.sha256(data).hexdigest(),
                        'executable': name.endswith('.elf')})
    manifest = {'schema': 1, 'kind': 'application', 'id': identity,
                'version': version, 'artifact': identity+'.elf',
                'architecture': 'xtensa-esp32s3', 'min_runtime_api': 2,
                'entries': entries,
                'requires': [{'capability': item['capability'], 'min_api': int(item['api'][2:])}
                             for item in requires or []]}
    (stage / '.package.json').write_text(json.dumps(manifest, separators=(',', ':')))
    output = root / 'dist/release-app-packages'
    output.mkdir(parents=True, exist_ok=True)
    name = f'application-{identity}-{version}-xtensa-esp32s3.rte.zip'
    archive = pack_directory(stage)
    (output / name).write_bytes(archive)
    catalog = {'schema': 1, 'release': 'unpublished-build',
               'packages': [catalog_row(stage, name, archive)]}
    (output / 'package-catalog.json').write_text(json.dumps(catalog))
    return {'source': source, 'sidecar': sidecar, 'manifest': manifest,
            'stage': stage, 'output': output, 'archive': archive, 'name': name,
            'source_path': app_source, 'payload': payload}


def stage_module(root, kind, identity, version='1.0.0'):
    """Exercise actual common ZIP/record plumbing for either new module kind."""
    source = {'type': kind, 'id': identity, 'version': version, 'driver_abi': 2,
              'architecture': 'xtensa-esp32s3', 'file_name': 'driver.elf',
              'provides': [{'capability': 'test.' + kind, 'api': 1}], 'requires': []}
    source_path = root / {'service': 'Services', 'provider': 'Providers'}[kind] / identity.replace('-', '_') / 'manifest.json'
    source_path.parent.mkdir(parents=True, exist_ok=True)
    source_path.write_text(json.dumps(source))
    stage = root / 'dist/packages' / identity
    stage.mkdir(parents=True, exist_ok=True)
    entries = []
    for name, data in [('driver.elf', b'fixture module'.ljust(64, b'\0')),
                       ('provider-abi.v1', f'os-cpu-abi=1\nprovides=test.{kind}\napi=1\n'.encode()),
                       ('privileged-imports.v1', b'\n'),
                       ('assets/help.txt', b'nested four-kind resource')]:
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        entries.append({'name': name, 'size_bytes': len(data),
                        'sha256': hashlib.sha256(data).hexdigest(), 'executable': name == 'driver.elf'})
    manifest = {'schema': 2, 'kind': kind, 'id': identity, 'version': version,
                'architecture': source['architecture'], 'artifact': 'driver.elf',
                'min_runtime_api': 2, 'entries': entries, 'requires': []}
    (stage / '.package.json').write_text(json.dumps(manifest, separators=(',', ':')))
    output = root / f'dist/release-{kind}-packages'
    output.mkdir(parents=True, exist_ok=True)
    name = f'{kind}-{identity}-{version}-xtensa-esp32s3.rte.zip'
    archive = pack_directory(stage)
    (output / name).write_bytes(archive)
    (output / 'package-catalog.json').write_text(json.dumps({'schema': 1, 'release': 'unpublished-build',
        'packages': [catalog_row(stage, name, archive)]}))
    return {'source': source, 'source_path': source_path, 'stage': stage,
            'manifest': manifest, 'output': output, 'name': name, 'archive': archive}


class ProductReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / 'dist/apps').mkdir(parents=True)
        self.stage = self.root / 'dist/packages/test-driver'
        self.stage.mkdir(parents=True)
        self.outputs = self.root / 'dist/release-packages'
        self.outputs.mkdir()
        source_dir = self.root / 'Drivers/test_driver'
        source_dir.mkdir(parents=True)
        self.source_path = source_dir / 'manifest.json'
        self.source = {'type': 'driver', 'id': 'test-driver', 'version': '2.0.1',
                       'driver_abi': 2, 'architecture': 'xtensa-esp32s3',
                       'file_name': 'driver.elf', 'requires': [],
                       'provides': [{'capability': 'test.provider', 'api': 1}]}
        self.source_path.write_text(json.dumps(self.source))
        entries = []
        for name, payload in [('driver.elf', b'fixture executable'.ljust(64, b'\x00')),
                              ('provider-abi.v1', b'os-cpu-abi=1\nprovides=test.provider\napi=1\n'),
                              ('privileged-imports.v1', b'import-list-v1\n'),
                              ('resource.dat', b'package resource')]:
            (self.stage / name).write_bytes(payload)
            entries.append({'name': name, 'size_bytes': len(payload),
                            'sha256': hashlib.sha256(payload).hexdigest(),
                            'executable': name == 'driver.elf'})
        self.manifest = {'schema': 1, 'kind': 'driver', 'id': 'test-driver',
                         'version': '2.0.1', 'architecture': 'xtensa-esp32s3',
                         'artifact': 'driver.elf', 'min_runtime_api': 2,
                         'entries': entries, 'requires': []}
        self.manifest_path = self.stage / '.package.json'
        self.manifest_path.write_text(json.dumps(self.manifest, separators=(',', ':')))
        self.name = 'driver-test-driver-2.0.1-xtensa-esp32s3.rte.zip'
        self.archive_path = self.outputs / self.name
        self.archive = pack_directory(self.stage)
        self.archive_path.write_bytes(self.archive)
        self.catalog_path = self.outputs / 'package-catalog.json'
        self.catalog = {'schema': 1, 'release': 'unpublished-build',
                        'packages': [catalog_row(self.stage, self.name, self.archive)]}
        self.catalog_path.write_text(json.dumps(self.catalog))

    def test_service_provider_records_and_cross_product_preservation(self):
        index = {'schema': 1, 'firmware': None, 'apps': [], 'drivers': []}
        for kind in ('service', 'provider'):
            fixture = stage_module(self.root, kind, kind + '-fixture')
            record = build_record(kind + 's', kind + '-fixture', '1.0.0', self.root)
            self.assertEqual(record['manifest']['kind'], kind)
            self.assertEqual(record['tag'], f'{kind}-{kind}-fixture-v1.0.0')
            self.assertEqual(record['sha256'], hashlib.sha256(fixture['archive']).hexdigest())
            index = update_index(index, kind + 's', record)
            wrong = copy.deepcopy(record)
            wrong.pop('format')
            with self.assertRaises(ValueError):
                validate_record(kind + 's', wrong)
            wrong = copy.deepcopy(record)
            wrong['manifest']['kind'] = 'driver'
            with self.assertRaises(ValueError):
                validate_record(kind + 's', wrong)
            wrong = copy.deepcopy(record)
            wrong['sha256'] = 'a' * 64
            with self.assertRaises(ValueError):
                update_index(index, kind + 's', wrong)
        retained = copy.deepcopy(index)
        index = update_index(index, 'drivers', self.record())
        stage_app(self.root)
        index = update_index(index, 'apps', build_record('apps', 'clock', '1.2.3', self.root))
        self.assertEqual(index['services'], retained['services'])
        self.assertEqual(index['providers'], retained['providers'])

    def test_export_kind_rejects_wrong_stage_before_output(self):
        sys.path.insert(0, str(ROOT / 'scripts'))
        import export_canonical_driver_release as exporter
        previous = exporter.SOURCE
        target = self.root / 'dist/wrong-kind-output'
        try:
            exporter.SOURCE = self.stage.parent
            with self.assertRaisesRegex(ValueError, 'wrong kind'):
                exporter.export({'test-driver'}, target, 'service')
            self.assertFalse(target.exists())
        finally:
            exporter.SOURCE = previous

    def test_module_record_rejects_wrong_source_and_restored_corruption(self):
        for kind in ('service', 'provider'):
            fixture = stage_module(self.root, kind, kind + '-fixture')
            source = fixture['source']
            source['provides'][0]['api'] = 2
            fixture['source_path'].write_text(json.dumps(source))
            with self.assertRaisesRegex(ValueError, 'ABI'):
                build_record(kind + 's', kind + '-fixture', '1.0.0', self.root)
            source['provides'][0]['api'] = 1
            fixture['source_path'].write_text(json.dumps(source))
            archive = fixture['output'] / fixture['name']
            archive.write_bytes(fixture['archive'][:-1])
            with self.assertRaises(ValueError):
                build_record(kind + 's', kind + '-fixture', '1.0.0', self.root)

    def rewrite_archive(self, raw=None, changes=None, extra=None):
        files = [('.package.json', raw if raw is not None else self.manifest_path.read_bytes())]
        files += [(entry['name'], (changes or {}).get(entry['name'],
                   (self.stage / entry['name']).read_bytes())) for entry in self.manifest['entries']]
        files += extra or []
        local, central = bytearray(), bytearray()
        for name, payload in files:
            encoded = name.encode('ascii')
            central += put_central(encoded, payload, crc32(payload), len(local))
            local += put_local(encoded, payload, crc32(payload))
        end = struct.pack('<IHHHHIIH', 0x06054B50, 0, 0, len(files), len(files),
                          len(central), len(local), 0)
        self.archive_path.write_bytes(local + central + end)

    def tearDown(self):
        self.temp.cleanup()

    def record(self):
        return build_record('drivers', 'test-driver', '2.0.1', self.root)

    def test_schema_two_nested_app_record_round_trip(self):
        app = stage_app(self.root)
        path = app['stage'] / 'assets/text/help.txt'
        path.parent.mkdir(parents=True)
        path.write_bytes(b'package-owned nested help')
        manifest = app['manifest']
        manifest['schema'] = 2
        manifest['entries'].append({'name': 'assets/text/help.txt', 'size_bytes': path.stat().st_size,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'executable': False})
        (app['stage'] / '.package.json').write_text(json.dumps(manifest, separators=(',', ':')))
        archive = pack_directory(app['stage'])
        (app['output'] / app['name']).write_bytes(archive)
        (app['output'] / 'package-catalog.json').write_text(json.dumps({'schema': 1,
            'release': 'unpublished-build', 'packages': [catalog_row(app['stage'], app['name'], archive)]}))
        record = build_record('apps', 'clock', '1.2.3', self.root)
        self.assertEqual(record['manifest'], manifest)
        self.assertEqual(validate_record('apps', record), dict(record, kind='app'))

    def test_driver_record_hashes_whole_zip_and_round_trips_index(self):
        record = self.record()
        self.assertEqual(record['format'], 'rte.zip')
        self.assertEqual(record['asset'], self.name)
        self.assertEqual(record['manifest'], self.manifest)
        self.assertEqual(record['size'], len(self.archive))
        self.assertEqual(record['sha256'], hashlib.sha256(self.archive).hexdigest())
        self.assertNotEqual(record['sha256'], self.manifest['entries'][0]['sha256'])
        empty = {'schema': 1, 'firmware': None, 'apps': [], 'drivers': []}
        updated = update_index(empty, 'drivers', record)
        parsed = json.loads(serialize_index(updated))
        self.assertEqual(parsed, update_index(parsed, 'drivers', record))
        self.assertEqual(validate_record('drivers', record), parsed['drivers'][0])
        self.assertEqual(empty['drivers'], [])

    def test_actual_offline_cli_record_and_atomic_index_update(self):
        output, index = self.root / 'record.json', self.root / 'index.json'
        index.write_text(json.dumps({'schema': 1, 'apps': [], 'drivers': []}))
        subprocess.run([sys.executable, str(ROOT / 'scripts/build_release_record.py'),
                        '--product', 'drivers', '--id', 'test-driver', '--version', '2.0.1',
                        '--root', str(self.root), '--output', str(output)], check=True,
                       capture_output=True)
        subprocess.run([sys.executable, str(ROOT / 'scripts/update_release_index.py'),
                        '--index', str(index), '--record', str(output), '--product', 'drivers'],
                       check=True, capture_output=True)
        self.assertEqual(json.loads(index.read_text())['drivers'][0]['asset'], self.name)

    def test_driver_record_ignores_retired_usb_catalog_and_loose_assets(self):
        (self.root / 'dist/packages/usb-provider-catalog.json').write_text('malformed obsolete input')
        (self.outputs / 'test-driver--driver.elf').write_bytes(b'wrong obsolete bytes')
        self.assertEqual(self.record()['asset'], self.name)

    def test_rejects_corrupted_or_appended_zip_bytes(self):
        for data in [self.archive[:-1], self.archive + b'append', b'PK\x03\x04bad']:
            with self.subTest(data_length=len(data)):
                self.archive_path.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.record()

    def test_rejects_changed_stage_entry_without_updated_hash(self):
        self.rewrite_archive(changes={'resource.dat': b'changed resource'})
        with self.assertRaisesRegex(ValueError, 'size/SHA-256 mismatch'):
            self.record()

    def test_rejects_catalog_hash_size_or_identity_mismatch(self):
        for field, value in [('sha256', '0' * 64), ('size_bytes', 1), ('version', '2.0.2'),
                             ('architecture', 'riscv32'), ('artifact', 'wrong.elf'),
                             ('archive', 'wrong.rte.zip')]:
            with self.subTest(field=field):
                catalog = copy.deepcopy(self.catalog)
                catalog['packages'][0][field] = value
                self.catalog_path.write_text(json.dumps(catalog))
                with self.assertRaisesRegex(ValueError, 'catalog does not uniquely match'):
                    self.record()

    def test_rejects_duplicate_catalog_identity_and_json_fields(self):
        self.catalog['packages'].append(self.catalog['packages'][0])
        self.catalog_path.write_text(json.dumps(self.catalog))
        with self.assertRaisesRegex(ValueError, 'uniquely match'):
            self.record()
        self.rewrite_archive(raw=self.manifest_path.read_bytes().replace(b'"schema":1', b'"schema":1,"schema":1'))
        with self.assertRaisesRegex(ValueError, 'duplicate JSON field'):
            self.record()

    def test_rejects_stale_source_and_dependency_mismatch(self):
        self.source['version'] = '2.0.2'
        self.source_path.write_text(json.dumps(self.source))
        with self.assertRaisesRegex(ValueError, 'stale'):
            self.record()
        self.source['version'] = '2.0.1'
        self.source['requires'] = [{'capability': 'test.bus', 'api': 1}]
        self.source_path.write_text(json.dumps(self.source))
        with self.assertRaisesRegex(ValueError, 'dependencies differ'):
            self.record()

    def test_rejects_symlinked_output_or_output_ancestor(self):
        saved = self.outputs / 'real-archive'
        self.archive_path.rename(saved)
        self.archive_path.symlink_to(saved)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.record()
        self.archive_path.unlink()
        saved.rename(self.archive_path)
        saved_outputs = self.outputs.with_name('real-outputs')
        self.outputs.rename(saved_outputs)
        self.outputs.symlink_to(saved_outputs, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.record()

    def test_requires_no_intermediate_stage_or_hidden_artifact_files(self):
        shutil.rmtree(self.stage)
        self.assertEqual(self.record()['asset'], self.name)

    def test_rejects_undeclared_zip_files_and_oversized_inputs(self):
        self.rewrite_archive(extra=[('unknown.dat', b'unknown data')])
        with self.assertRaisesRegex(ValueError, 'undeclared'):
            self.record()
        with self.archive_path.open('wb') as stream:
            stream.truncate(8 * 1024 * 1024)
        with self.assertRaisesRegex(ValueError, 'oversized'):
            self.record()

    def test_rejects_escaped_manifest_aliases_and_unsafe_zip_paths(self):
        self.rewrite_archive(raw=self.manifest_path.read_bytes().replace(b'driver', b'dri\\u0076er', 1))
        with self.assertRaisesRegex(ValueError, 'JSON escapes'):
            self.record()
        self.rewrite_archive(extra=[('../escape', b'no extraction')])
        with self.assertRaisesRegex(ValueError, 'unsafe'):
            self.record()
        self.assertFalse((self.root / 'escape').exists())

    def test_rejects_source_capability_and_bundled_abi_disagreement(self):
        self.source['provides'][0]['capability'] = 'different.provider'
        self.source_path.write_text(json.dumps(self.source))
        with self.assertRaisesRegex(ValueError, 'provider ABI differs'):
            self.record()

    def test_rejects_unsafe_requested_identity(self):
        with self.assertRaisesRegex(ValueError, 'unsafe canonical'):
            build_record('drivers', '../escape', '2.0.1', self.root)

    def test_app_record_hashes_whole_bundle_and_keeps_sidecar_inside(self):
        app = stage_app(self.root, requires=[{'capability': 'input.touch.raw', 'api': '>=1'}])
        record = build_record('apps', 'clock', '1.2.3', self.root)
        self.assertEqual(record['format'], 'rte.zip')
        self.assertEqual(record['manifest'], app['manifest'])
        self.assertEqual(record['asset'], app['name'])
        self.assertEqual(record['size'], len(app['archive']))
        self.assertEqual(record['sha256'], hashlib.sha256(app['archive']).hexdigest())
        self.assertNotEqual(record['sha256'], app['sidecar']['sha256'])
        self.assertEqual(validate_record('apps', record)['kind'], 'app')

    def test_app_bundle_restore_needs_no_intermediate_or_loose_artifacts(self):
        stage_app(self.root)
        expected = build_record('apps', 'clock', '1.2.3', self.root)
        shutil.rmtree(self.root / 'dist/packages')
        shutil.rmtree(self.root / 'dist/apps')
        self.assertEqual(build_record('apps', 'clock', '1.2.3', self.root), expected)

    def test_app_source_and_sidecar_remain_bound_to_bundled_content(self):
        app = stage_app(self.root)
        source = dict(app['source'], display_name='Unexpected replacement')
        app['source_path'].write_text(json.dumps(source))
        with self.assertRaisesRegex(ValueError, 'sidecar differs'):
            build_record('apps', 'clock', '1.2.3', self.root)
        source['version'] = '1.2.4'
        app['source_path'].write_text(json.dumps(source))
        with self.assertRaisesRegex(ValueError, 'stale'):
            build_record('apps', 'clock', '1.2.3', self.root)

    def test_legacy_app_transition_requires_newer_version(self):
        app = stage_app(self.root)
        legacy = {'kind': 'app', 'id': 'clock', 'version': '1.2.3',
                  'tag': 'app-clock-v1.2.3', 'asset': 'clock.elf',
                  'url': 'https://github.com/michaelrolphone-cmyk/T5S3-Reader/releases/download/app-clock-v1.2.3/clock.elf',
                  'size': len(app['payload']), 'sha256': app['sidecar']['sha256'],
                  'manifest': app['sidecar']}
        index = update_index({'schema': 1, 'apps': [], 'drivers': []}, 'apps', legacy)
        with self.assertRaisesRegex(ValueError, 'different content'):
            update_index(index, 'apps', build_record('apps', 'clock', '1.2.3', self.root))
        stage_app(self.root, version='1.2.4')
        updated = update_index(index, 'apps', build_record('apps', 'clock', '1.2.4', self.root))
        self.assertEqual(updated['apps'][0]['version'], '1.2.4')
        self.assertEqual(index['apps'][0], legacy)

    def test_app_and_driver_catalogs_do_not_overwrite_each_other(self):
        driver = self.record()
        stage_app(self.root)
        self.assertEqual(self.record(), driver)
        self.assertEqual(build_record('apps', 'clock', '1.2.3', self.root)['manifest']['kind'], 'application')

    def test_firmware_record_keeps_existing_asset_contract(self):
        data = b'firmware image'
        (self.root / 'dist/firmware-t5s3-pro.bin').write_bytes(data)
        record = build_record('firmware', '', '1.2.3', self.root)
        self.assertEqual(record['tag'], 'firmware-v1.2.3')
        self.assertEqual(record['asset'], 'firmware-t5s3-pro.bin')
        self.assertEqual(record['sha256'], hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    unittest.main()
