#!/usr/bin/env python3
"""A fourth class package must join without changing a fixed identity list."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_installed_usb_stack as builder


class ProviderDiscoveryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.drivers = root / 'Drivers'
        self.artifacts = root / 'dist' / 'experimental'
        self.drivers.mkdir()
        self.artifacts.mkdir(parents=True)
        old_drivers, old_artifacts = builder.DRIVER_SOURCES, builder.SOURCE
        builder.DRIVER_SOURCES, builder.SOURCE = self.drivers, self.artifacts
        self.addCleanup(setattr, builder, 'DRIVER_SOURCES', old_drivers)
        self.addCleanup(setattr, builder, 'SOURCE', old_artifacts)

    def provider(self, folder: str, identity: str, capability: str,
                 requires=(), artifact: bool = True, version: str = '1.0.0', api: int = 1):
        directory = self.drivers / folder
        directory.mkdir()
        requirements = [({'capability': name, 'api': minimum}
                         if isinstance(name, str) else name)
                        for name, minimum in ((item, 1) if isinstance(item, str)
                                              else item for item in requires)]
        metadata = {'type': 'driver', 'id': identity, 'version': version,
                    'driver_abi': 2, 'architecture': 'xtensa-esp32s3',
                    'file_name': 'driver.elf',
                    'provides': [{'capability': capability, 'api': api}],
                    'requires': requirements}
        (directory / 'manifest.json').write_text(json.dumps(metadata), encoding='utf-8')
        if artifact:
            output = self.artifacts / identity
            output.mkdir()
            (output / 'driver.elf').write_bytes(b'\x7fELF' + bytes(64))

    def test_additional_class_and_dependency_order(self):
        self.provider('class_extra', 'test-serial-class', 'serial.port', ['usb.host'])
        self.provider('usb_host', 'usb-host', 'usb.host', ['usb.controller'])
        self.provider('controller', 'usb-controller', 'usb.controller')
        discovered = builder.discovered()
        self.assertEqual({item['id'] for item in discovered},
                         {'test-serial-class', 'usb-host', 'usb-controller'})
        self.assertEqual([item['id'] for item in builder.dependency_order(discovered)],
                         ['usb-controller', 'usb-host', 'test-serial-class'])

    def test_alternative_class_capability_is_not_identity_collision(self):
        self.provider('class_a', 'class-a', 'serial.port')
        self.provider('class_b', 'class-b', 'serial.port')
        self.assertEqual(len(builder.dependency_order(builder.discovered())), 2)

    def test_missing_or_ambiguous_artifact_fails_closed(self):
        self.provider('class_a', 'class-a', 'serial.port', artifact=False)
        with self.assertRaises((ValueError, FileNotFoundError)):
            builder.discovered()
        output = self.artifacts / 'class-a'
        output.mkdir()
        (output / 'driver.elf').write_bytes(b'\x7fELF' + bytes(64))
        (output / 'unexpected.elf').write_bytes(b'\x7fELF' + bytes(64))
        with self.assertRaises(ValueError):
            builder.discovered()

    def test_duplicate_identity_and_invalid_version_fail(self):
        self.provider('class_a', 'class-a', 'serial.port')
        self.provider('class_b', 'class-a', 'serial.port', artifact=False)
        with self.assertRaises(ValueError):
            builder.discovered()
        (self.drivers / 'class_b' / 'manifest.json').unlink()
        self.provider('class_c', 'class-c', 'other.class', version='legacy')
        with self.assertRaises(ValueError):
            builder.discovered()

    def test_unavailable_dependency_and_cycles_fail(self):
        self.provider('class_a', 'class-a', 'serial.port', ['usb.host'])
        with self.assertRaises(ValueError):
            builder.dependency_order(builder.discovered())
        self.provider('host', 'host', 'usb.host', ['serial.port'])
        with self.assertRaises(ValueError):
            builder.dependency_order(builder.discovered())

    def test_api_floor_is_not_just_a_capability_name(self):
        self.provider('host_old', 'host-old', 'usb.host', api=1)
        self.provider('serial', 'serial', 'serial.port', [('usb.host', 2)])
        with self.assertRaises(ValueError):
            builder.dependency_order(builder.discovered())
        self.provider('host_new', 'host-new', 'usb.host', api=2)
        order = builder.dependency_order(builder.discovered())
        self.assertLess([x['id'] for x in order].index('host-new'),
                        [x['id'] for x in order].index('serial'))


if __name__ == '__main__':
    unittest.main()
