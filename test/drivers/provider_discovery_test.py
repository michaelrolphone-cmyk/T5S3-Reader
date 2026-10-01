#!/usr/bin/env python3
"""A fourth class package must join without changing a fixed identity list."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

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
        self.packages = root / 'dist' / 'packages'
        self.drivers.mkdir()
        self.artifacts.mkdir(parents=True)
        old_drivers, old_artifacts = builder.DRIVER_SOURCES, builder.SOURCE
        old_packages = builder.DESTINATION
        builder.DESTINATION = self.packages
        self.addCleanup(setattr, builder, 'DESTINATION', old_packages)
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

    def test_retired_cdc_alias_is_not_a_current_provider(self):
        self.provider('old_cdc', 'usb-cdc-acm-v2', 'serial.port')
        with self.assertRaisesRegex(ValueError, 'retired CDC alias'):
            builder.source_candidates()

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

    def build_fixture_packages(self, identities=None):
        # Exercise selection, real manifests and real ZIP/catalog output. ELF
        # relocation/import verification has separate actual-artifact tests.
        def inputs(_elf, manifest, target):
            capability, api = builder.canonical_manifest(manifest)
            (target / 'provider-abi.v1').write_text(
                f'os-cpu-abi=1\nprovides={capability}\napi={api}\n')
            (target / 'privileged-imports.v1').write_text('\n')
        mapping = {'unmapped_relocations': [], 'unmapped_relative_values': [],
                   'unmapped_executable_sections': [], 'absolute_peripheral_relocations': []}
        with patch.object(builder, 'audit_loader_map', return_value=mapping), \
                patch.object(builder, 'extract_imports', return_value=[]), \
                patch.object(builder, 'provider_inputs', side_effect=inputs), \
                patch.object(builder.subprocess, 'run',
                             side_effect=AssertionError('unselected provider was built')):
            return builder.build(identities)

    def test_software_service_uses_same_discovery_and_bundle_engine(self):
        self.provider('clock', 'clock', 'platform.clock')
        self.provider('archive_zip', 'archive-zip', 'archive.zip', ['platform.clock'])
        services = self.drivers.parent / 'Services'
        services.mkdir()
        (self.drivers / 'archive_zip').rename(services / 'archive_zip')
        path = services / 'archive_zip/manifest.json'
        manifest = json.loads(path.read_text()); manifest['type'] = 'service'
        path.write_text(json.dumps(manifest))
        catalog = self.build_fixture_packages()
        self.assertEqual([(row['id'], row['kind']) for row in catalog],
                         [('clock', 'driver'), ('archive-zip', 'service')])
        self.assertTrue(catalog[1]['archive'].startswith('service-archive-zip-'))
        package = json.loads((self.packages / 'archive-zip/.package.json').read_text())
        self.assertEqual(package['kind'], 'service')
        self.assertEqual(package['requires'], [{'capability': 'platform.clock', 'min_api': 1}])

    def test_selected_build_skips_other_artifacts_and_keeps_dependency_metadata(self):
        self.provider('dependency', 'required-provider', 'test.clock', artifact=False)
        self.provider('selected', 'selected-provider', 'test.sensor', ['test.clock'])
        self.provider('unrelated', 'unrelated-provider', 'other.device', artifact=False)
        with patch.object(builder, 'linked_or_build', wraps=builder.linked_or_build) as link:
            catalog = self.build_fixture_packages({'selected-provider'})
        self.assertEqual([call.args[0] for call in link.call_args_list], ['selected-provider'])
        self.assertEqual([row['id'] for row in catalog], ['selected-provider'])
        self.assertEqual({path.name for path in self.packages.iterdir()}, {
            'selected-provider', 'package-catalog.json', catalog[0]['archive']})
        manifest = json.loads((self.packages / 'selected-provider/.package.json').read_text())
        self.assertEqual(manifest['requires'], [{'capability': 'test.clock', 'min_api': 1}])
        self.assertEqual(json.loads((self.packages / 'package-catalog.json').read_text())['packages'],
                         catalog)

    def test_default_build_still_exports_every_provider_in_dependency_order(self):
        self.provider('consumer', 'consumer', 'test.sensor', ['test.clock'])
        self.provider('clock', 'clock', 'test.clock')
        with patch.object(builder, 'linked_or_build', wraps=builder.linked_or_build) as link:
            catalog = self.build_fixture_packages()
        self.assertEqual([row['id'] for row in catalog], ['clock', 'consumer'])
        self.assertEqual([call.args[0] for call in link.call_args_list], ['clock', 'consumer'])
        self.assertEqual({path.name for path in self.packages.iterdir() if path.is_dir()},
                         {'clock', 'consumer'})

    def test_invalid_selection_and_source_graph_fail_before_any_build(self):
        self.provider('selected', 'selected', 'test.sensor', artifact=False)
        with patch.object(builder, 'linked_or_build') as link:
            for identities in (set(), {'missing'}):
                with self.subTest(identities=identities), self.assertRaises(ValueError):
                    builder.build(identities)
            link.assert_not_called()
        self.provider('broken', 'broken', 'other.sensor', ['missing.capability'], artifact=False)
        with patch.object(builder, 'linked_or_build') as link:
            with self.assertRaises(ValueError):
                builder.build({'selected'})
            link.assert_not_called()
        self.assertFalse(self.packages.exists())


if __name__ == '__main__':
    unittest.main()
