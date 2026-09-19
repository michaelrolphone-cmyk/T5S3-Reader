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
from driver_package import REQUIRES, PROVIDES, USB_REQUIRES, USB_PROVIDES, validate_manifest, validate_payload
from install_driver import install

ROOT = Path(__file__).resolve().parents[2]

class UsbCdcPackage(unittest.TestCase):
    def setUp(self):
        self.elf = bytearray(64)
        self.elf[:7] = b'\x7fELF\x01\x01\x01'
        struct.pack_into('<HH', self.elf, 16, 3, 94)
        self.manifest = dict(type='driver', id='usb-cdc-acm', version='0.1.0', driver_abi=1,
                             architecture='xtensa-esp32s3', file_name='driver.elf',
                             requires=USB_REQUIRES, provides=USB_PROVIDES,
                             size_bytes=len(self.elf), sha256=hashlib.sha256(self.elf).hexdigest())
    def package(self):
        file = io.BytesIO()
        with zipfile.ZipFile(file, 'w') as archive:
            archive.writestr('manifest.json', json.dumps(self.manifest))
            archive.writestr('driver.elf', self.elf)
        file.seek(0)
        return file
    def test_install_and_integrity(self):
        validate_payload(self.manifest, self.elf)
        with tempfile.TemporaryDirectory() as tmp:
            target = install(self.package(), Path(tmp))
            self.assertEqual(target, Path(tmp) / 'Drivers/usb-cdc-acm')
            self.assertEqual((target / 'driver.elf').read_bytes(), self.elf)
            self.manifest['version'] = '0.1.1'
            install(self.package(), Path(tmp))
            self.assertTrue((Path(tmp) / 'Drivers/.usb-cdc-acm.previous/driver.elf').exists())
        with self.assertRaises(ValueError):
            validate_payload({**self.manifest, 'sha256': '0' * 64}, self.elf)
    def test_reject_spoofing(self):
        for patch in ({'id': '../../escape'}, {'id': 'gps-nmea'},
                      {'requires': []}, {'provides': [{'capability': 'position.gnss', 'api': 1}]},
                      {'driver_abi': 2}, {'architecture': 'xtensa-esp32'},
                      {'file_name': '../driver.elf'}):
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                validate_manifest({**self.manifest, **patch})
    def test_catalog_contains_all_published_drivers(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            gps = {**self.manifest, 'id': 'gps-nmea', 'version': '1.0.0',
                   'requires': REQUIRES, 'provides': PROVIDES}
            for manifest in (gps, self.manifest):
                stem = f"{manifest['id']}-{manifest['version']}.t5driver"
                (directory / f'{stem}.json').write_text(json.dumps(manifest), encoding='utf-8')
                (directory / f'{stem}.elf').write_bytes(self.elf)
            catalog = json.loads(write_release_catalog(directory).read_text(encoding='utf-8'))
            self.assertEqual(catalog['schema'], 1)
            self.assertEqual({entry['manifest']['id'] for entry in catalog['drivers']},
                             {'gps-nmea', 'usb-cdc-acm'})
            self.assertEqual({entry['elf_asset'] for entry in catalog['drivers']},
                             {'gps-nmea-1.0.0.t5driver.elf', 'usb-cdc-acm-0.1.0.t5driver.elf'})
    def test_installed_usb_cdc_elf_activation_uses_explicit_json_strings(self):
        source = (ROOT / 'src/runtime/drivers/UsbCdcDriverRuntime.cpp').read_text(encoding='utf-8')
        self.assertNotIn(' | nullptr;', source)
        self.assertIn('const JsonDocument& view = doc;', source)
        self.assertIn('view["requires"].is<JsonArrayConst>()', source)
        self.assertIn('view["provides"].is<JsonArrayConst>()', source)
        self.assertIn('view["requires"][0]["capability"].as<const char*>()', source)
        self.assertIn('view["provides"][0]["capability"].as<const char*>()', source)
        self.assertIn('USBREF phase=driver_load result=active', source)
    def test_usb_class_descriptor_and_physical_ownership_are_in_elves(self):
        bridge = (ROOT / 'src/native/NativeUsbBridge.cpp').read_text(encoding='utf-8')
        controller = (ROOT / 'Drivers/usb_controller_esp32s3/driver.cpp').read_text(encoding='utf-8')
        cdc = (ROOT / 'Drivers/usb_cdc_v2/driver.c').read_text(encoding='utf-8')
        self.assertIn('RuntimeInstalledProviders::acquire(', bridge)
        self.assertIn('RuntimeInstalledProviders::shutdown()', bridge)
        self.assertNotIn('#include <usb/usb_host.h>', bridge)
        # Hardware ownership checks apply to source statements, not comments
        # documenting the ELF host; do not weaken checks for executable calls.
        bridge_code = '\n'.join(line for line in bridge.splitlines()
                                if not line.lstrip().startswith('//'))
        self.assertNotIn('usb_host_install(', bridge_code)
        self.assertNotIn('Wire.beginTransmission(', bridge_code)
        # v1.2.16 released the shared debug USB PHY before host initialization.
        # Guard this handoff against another firmware/ELF migration regression.
        startup = bridge.split('bool serialStart(', 1)[1]
        self.assertIn('Serial.end();', bridge)
        self.assertIn('Serial.begin(115200);', bridge)
        self.assertLess(startup.index('suspendDebugConsole();'),
                        startup.index('RuntimeInstalledProviders::acquire('))
        self.assertIn('#include <usb/usb_host.h>', controller)
        self.assertIn('usb_host_install(', controller)
        self.assertIn('host->bulk_read(', cdc)
        self.assertIn('host->bulk_write(', cdc)

if __name__ == '__main__': unittest.main()
