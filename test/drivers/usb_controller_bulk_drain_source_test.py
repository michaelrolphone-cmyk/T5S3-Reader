#!/usr/bin/env python3
"""Source-boundary guard for IDF-backed USB controller teardown.

This cannot model IDF callback timing or substitute for a target build. It
prevents accidental replacement of the controller's combined drain/retry path
with either an in-flight VBUS leak or a double interface release.
"""
from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Drivers/usb_controller_esp32s3/driver_base.cpp'
MANIFEST = ROOT / 'Drivers/usb_controller_esp32s3/manifest.json'


class UsbControllerDrainTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding='utf-8')

    def section(self, start: str, end: str) -> str:
        return self.source.split(start, 1)[1].split(end, 1)[0]

    def test_bulk_timeout_reclaims_callback_and_bounds_work(self):
        drain = self.section('bool drain_bulk(', 'bool wait_completion(')
        wait = self.section('bool wait_completion(', 'bool idle_transfer(')
        for operation in ('usb_host_endpoint_halt(', 'usb_host_endpoint_flush(',
                          'while (inFlight)', 'pump(1)', 'xTaskGetTickCount()',
                          'usb_host_endpoint_clear('):
            self.assertIn(operation, drain)
        self.assertIn('kTeardownTicks', drain)
        self.assertIn('drain_bulk(true)', wait)
        self.assertIn('USBCTRL failure=bulk-timeout-undrained', wait)
        self.assertIn('transfer->bEndpointAddress', wait)

    def test_release_drains_only_its_own_bulk_before_two_stage_cleanup(self):
        release = self.section('bool release_interface(', 'int32_t control(')
        self.assertIn('transfer->device_handle != d->handle', release)
        self.assertIn('endpoint_mps(d, c->number, c->alternate,', release)
        self.assertIn('!drain_bulk(false)', release)
        self.assertIn('c->interfaceReleased', release)
        self.assertIn('RiscUsbController::releaseClaim(', release)
        self.assertLess(release.index('drain_bulk(false)'),
                        release.index('RiscUsbController::releaseClaim('))
        self.assertLess(release.index('RiscUsbController::releaseClaim('),
                        release.index('*c = {};'))
        self.assertIn('c->interfaceReleased', self.section('int32_t bulk(', 'int32_t bulk_read('))
        self.assertIn('reapDetachedUnclaimed();', self.section('int32_t next_event(',
                                                              'bool configuration('))

    def test_quiesce_drains_before_dma_free_and_vbus_release(self):
        quiesce = self.section('bool quiesce_host() {', 'void stop()')
        self.assertIn('if (inFlight && !drain_bulk(false)) return false;', quiesce)
        self.assertLess(quiesce.index('drain_bulk(false)'),
                        quiesce.index('usb_host_transfer_free('))
        self.assertLess(quiesce.index('usb_host_uninstall()'),
                        quiesce.index('power->release_host('))
        self.assertIn('USBCTRL stage=vbus-released', quiesce)
        self.assertIn('usb_host_lib_handle_events(0, &finalFlags)', quiesce)
        self.assertNotIn('NativeUsbBridge', self.section('bool drain_bulk(',
                                                         'bool wait_completion('))

    def test_distributable_version_exceeds_current_master_lineage(self):
        manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
        self.assertEqual(manifest['id'], 'usb-controller-esp32s3')
        self.assertGreater(tuple(map(int, manifest['version'].split('.'))), (0, 1, 4))


if __name__ == '__main__':
    unittest.main()
