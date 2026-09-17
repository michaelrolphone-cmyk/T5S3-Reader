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
    def test_firmware_manifest_reader_const_array_contract(self):
        source = (Path(__file__).resolve().parents[2] /
                  'src/runtime/drivers/DriverPackage.cpp').read_text(encoding='utf-8')
        self.assertNotIn('.is<JsonArray>()', source)
        self.assertIn('const JsonDocument& view = doc;', source)
        for key in ('requires', 'provides'):
            self.assertIn(f'view["{key}"].is<JsonArrayConst>()', source)
            self.assertIn(f'doc["{key}"].is<JsonArrayConst>()', source)
        self.assertIn('const JsonArrayConst provided = view["provides"].as<JsonArrayConst>();', source)
        self.assertIn('provided[0]["capability"]', source)
    def test_firmware_json_string_extraction_and_asset_naming(self):
        root = Path(__file__).resolve().parents[2]
        parser = (root / 'src/runtime/drivers/DriverPackage.cpp').read_text(encoding='utf-8')
        manager = (root / 'src/native/NativeDriverManagerBridge.cpp').read_text(encoding='utf-8')
        self.assertNotIn(' | nullptr;', parser)
        self.assertNotIn('entry["elf_asset"] | nullptr', manager)
        for field in ('id', 'version', 'sha256'):
            self.assertIn(f'view["{field}"].as<const char*>()', parser)
        self.assertIn('entry["capability"].as<const char*>()', parser)
        self.assertIn('provided[0]["capability"].as<const char*>()', parser)
        self.assertIn('doc["sha256"].as<const char*>()', parser)
        self.assertIn('entry["elf_asset"].as<const char*>()', manager)
        self.assertIn('std::string(info.id) + "-" + info.version + ".t5driver.elf"', manager)
        self.assertIn('Storage.rename(kDownloadedStorage, stageElf.c_str())', parser)
        self.assertIn('"/driver.elf"', parser)
    def test_firmware_install_writable_storage_contract(self):
        root = Path(__file__).resolve().parents[2]
        parser = (root / 'src/runtime/drivers/DriverPackage.cpp').read_text(encoding='utf-8')
        vfs = (root / 'lib/NativeApps/src/SdVfs.cpp').read_text(encoding='utf-8')
        hal = (root / 'lib/hal/HalStorage.h').read_text(encoding='utf-8')
        self.assertIn('errno = EROFS;', vfs)
        for forbidden in ('::mkdir(', 'std::rename(', 'std::remove(', '::rmdir(', 'std::fopen(path, "wb")'):
            self.assertNotIn(forbidden, parser)
        for required in ('Storage.rename(', 'Storage.mkdir(', 'Storage.rmdir(',
                         'Storage.writeFile(', 'Storage.exists(', 'Storage.remove('):
            self.assertIn(required, parser)
        for api in ('bool rename(', 'bool mkdir(', 'bool rmdir(', 'bool writeFile('):
            self.assertIn(api, hal)
        self.assertIn('"/sd" + stageElf', parser)
        self.assertIn('"/Drivers/.driver-manager.part"', parser)
        self.assertIn('ROLLBACK FAILED', parser)
        self.assertIn('ELF validation: SHA-256 mismatch', parser)
        self.assertIn('Storage.exists((storagePath + "/driver.elf").c_str())', parser)
    def test_release_discovery_diagnostic_guards(self):
        root = Path(__file__).resolve().parents[2]
        parser = (root / 'src/runtime/drivers/DriverPackage.cpp').read_text(encoding='utf-8')
        manager = (root / 'src/native/NativeDriverManagerBridge.cpp').read_text(encoding='utf-8')
        self.assertIn('Manifest rejected at %s', parser)
        self.assertIn('Manifest rejected at JSON decoding', parser)
        for field in ('type', 'architecture', 'file_name', 'driver_abi', 'size_bytes type',
                      'sha256 type', 'requires array', 'provides array'):
            self.assertIn(f', "{field}");', parser)
        self.assertIn('Aggregate catalog HTTP fetch failed', manager)
        self.assertIn('Aggregate catalog JSON decode failed', manager)
        self.assertIn('Aggregate catalog entry[%u] manifest rejected', manager)
        self.assertIn('manifest.size() != manifestAsset.size', manager)
        self.assertIn('Driver manifest length mismatch', manager)
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
