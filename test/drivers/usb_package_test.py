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
from driver_package import USB_REQUIRES, USB_PROVIDES, validate_manifest, validate_payload
from install_driver import install

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

if __name__ == '__main__': unittest.main()
