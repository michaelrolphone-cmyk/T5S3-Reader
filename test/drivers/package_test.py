import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from build_driver import write_release_catalog
from driver_package import REQUIRES, PROVIDES, read_json, validate_payload
from install_driver import install

class Packages(unittest.TestCase):
    def setUp(self):
        self.elf = bytearray(64)
        self.elf[:7] = b'\x7fELF\x01\x01\x01'
        struct.pack_into('<HH', self.elf, 16, 3, 94)
        self.manifest = dict(type='driver', id='gps-nmea', version='1.0.0', driver_abi=1,
                             architecture='xtensa-esp32s3', file_name='driver.elf', requires=REQUIRES,
                             provides=PROVIDES, size_bytes=64, sha256=hashlib.sha256(self.elf).hexdigest())
    def package(self, extra=False):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, 'w') as z:
            z.writestr('driver.elf', self.elf)
            z.writestr('manifest.json', json.dumps(self.manifest))
            if extra: z.writestr('../escape', 'bad')
        stream.seek(0)
        return stream
    def test_contract(self):
        validate_payload(self.manifest, self.elf)
        for key, value in [('driver_abi', 2), ('architecture', 'arm'), ('file_name', '../escape'),
                           ('sha256', '0' * 64), ('size_bytes', 65), ('requires', [])]:
            bad = {**self.manifest, key: value}
            with self.assertRaises(ValueError): validate_payload(bad, self.elf)
        with self.assertRaises(ValueError): read_json(b'{"id":"one","id":"two"}')
    def test_release_catalog(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            manifest_asset = directory / 'gps-nmea-1.0.0.t5driver.json'
            elf_asset = directory / 'gps-nmea-1.0.0.t5driver.elf'
            manifest_asset.write_text(json.dumps(self.manifest), encoding='utf-8')
            elf_asset.write_bytes(self.elf)
            catalog_path = write_release_catalog(directory)
            catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
            self.assertEqual(catalog['schema'], 1)
            self.assertEqual(len(catalog['drivers']), 1)
            self.assertEqual(catalog['drivers'][0]['manifest'], self.manifest)
            self.assertEqual(catalog['drivers'][0]['elf_asset'], elf_asset.name)
    def test_install_update_and_rollback_copy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = install(self.package(), root)
            self.assertEqual((target / 'driver.elf').read_bytes(), self.elf)
            self.manifest['version'] = '1.0.1'
            install(self.package(), root)
            previous = root / 'Drivers/.gps-nmea.previous'
            self.assertEqual(read_json((previous / 'manifest.json').read_bytes())['version'], '1.0.0')
            self.assertEqual(read_json((target / 'manifest.json').read_bytes())['version'], '1.0.1')
            with self.assertRaises(ValueError): install(self.package(), root)
    def test_reject_before_mutation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaises(ValueError): install(self.package(extra=True), root)
            self.manifest['sha256'] = '0' * 64
            with self.assertRaises(ValueError): install(self.package(), root)
            self.assertFalse((root / 'Drivers').exists())

if __name__ == '__main__': unittest.main()
