import hashlib
import io
import json
import re
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
    def physical_controller(self):
        wrapper = (ROOT / 'Drivers/usb_controller_esp32s3/driver.cpp').read_text(encoding='utf-8')
        # HID extended the SAME executable using a source include. The base
        # remains hardware-owning and is compiled directly into that ELF.
        self.assertIn('#include "driver_base.cpp"', wrapper)
        self.assertIn('interrupt_read(', wrapper)
        base = (ROOT / 'Drivers/usb_controller_esp32s3/driver_base.cpp').read_text(encoding='utf-8')
        self.assertIn('#include "HostStartup.h"', base)
        startup = (ROOT / 'Drivers/usb_controller_esp32s3/HostStartup.h').read_text(encoding='utf-8')
        return base.replace('#include "HostStartup.h"', startup)
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
        controller = self.physical_controller()
        cdc = (ROOT / 'Drivers/usb_cdc_v2/driver.c').read_text(encoding='utf-8')
        self.assertIn('RuntimeInstalledProviders::acquire(', bridge)
        # A comment mentioning shutdown must never pass this test: serial
        # stop releases only its class/host grants, not unrelated providers.
        self.assertNotIn('if (!RuntimeInstalledProviders::shutdown())', bridge)
        self.assertIn('if (!closeClass()) return;', bridge)
        self.assertIn('!RuntimeInstalledProviders::release(&hostGrant)', bridge)
        self.assertNotIn('#include <usb/usb_host.h>', bridge)
        bridge_code = '\n'.join(line for line in bridge.splitlines()
                                if not line.lstrip().startswith('//'))
        self.assertNotIn('usb_host_install(', bridge_code)
        self.assertNotIn('Wire.beginTransmission(', bridge_code)
        self.assertNotIn('Serial.end();', bridge)
        self.assertNotIn('Serial.begin(', bridge)
        self.assertIn('#include <usb/usb_host.h>', controller)
        self.assertIn('usb_host_install(', controller)
        self.assertIn('host->bulk_read(', cdc)
        self.assertIn('host->bulk_write(', cdc)
    def test_vbus_cleanup_is_reached_from_normal_and_elf_error_return(self):
        app = (ROOT / 'Apps/serial_monitor_implementation.inc').read_text(encoding='utf-8')
        host = (ROOT / 'src/native/NativeAppHost.cpp').read_text(encoding='utf-8')
        stream_path = ROOT / 'src/native/NativeStreamBridge.cpp'
        streams = stream_path.read_text(encoding='utf-8')
        streams = re.sub(r'#include "(NativeStreamBridge\.p[0-9]+\.inc)"',
                         lambda match: (stream_path.parent / match[1]).read_text(), streams)
        serial = (ROOT / 'src/native/NativeSerialPortBridge_implementation.inc').read_text(encoding='utf-8')
        bridge = (ROOT / 'src/native/NativeUsbBridge.cpp').read_text(encoding='utf-8')
        controller = self.physical_controller()
        self.assertIn('if (event.type == T5_UI_EVENT_EXIT) {\n            release_serial_session();', app)
        self.assertLess(host.index('const esp_err_t result = launch_elf_app(path);'),
                        host.index('nativeStreamsEnd();'))
        self.assertIn('invocation.end();', streams)
        self.assertIn('providers.end();', serial)
        cleanup = serial.split('void nativeSerialPortsEnd() {', 1)[1]
        self.assertIn('if (providers.leased() || leaseHandle)', cleanup)
        self.assertIn('context-end-quarantined', cleanup)
        self.assertIn('!RuntimeInstalledProviders::release(&hostGrant)', bridge)
        self.assertIn('RuntimeInstalledProviders::recoverFailedProvider(failedHostId', bridge)
        self.assertNotIn('RuntimeInstalledProviders::shutdown()', bridge)
        self.assertIn('restore_phy_route();', controller)
        self.assertIn('power->release_host(power->context, powerLease)', controller)
    def test_usb_bulk_timeout_must_reclaim_callback_before_vbus_release(self):
        source = self.physical_controller()
        wait = source.split('bool wait_completion(', 1)[1].split('bool idle_transfer(', 1)[0]
        release = source.split('bool release_interface(', 1)[1].split('int32_t control(', 1)[0]
        quiesce = source.split('bool quiesce_host() {', 1)[1].split('void stop()', 1)[0]
        drain = source.split('bool drain_bulk(', 1)[1].split('bool wait_completion(', 1)[0]
        self.assertIn('usb_host_endpoint_halt(', drain)
        self.assertIn('usb_host_endpoint_flush(', drain)
        self.assertIn('while (inFlight)', drain)
        self.assertIn('usb_host_endpoint_clear(', drain)
        self.assertIn('drain_bulk(true)', wait)
        self.assertLess(release.index('drain_bulk(false)'), release.index('usb_host_interface_release('))
        self.assertLess(quiesce.index('drain_bulk(false)'), quiesce.index('usb_host_transfer_free('))
        self.assertLess(quiesce.index('usb_host_uninstall()'),
                        quiesce.index('power->release_host('))
        self.assertIn('USBCTRL stage=vbus-released', quiesce)

if __name__ == '__main__': unittest.main()
