#!/usr/bin/env python3
"""Verify all discovered linked provider packages, imports, pointers and ZIP catalog."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import struct
import sys
import tempfile
import traceback
import unittest
from zipfile import ZipFile, ZIP_STORED

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_privileged_imports_v1 import extract_imports, encode_imports
from verify_provider_relocation_map import audit_loader_map
from provider_discovery_test import ProviderDiscoveryTest
PACKAGES = ROOT / 'dist/packages'
CATALOG = PACKAGES / 'package-catalog.json'
BRIDGE = 'risc_fw_i2c_transact_v1'
# Baseline U1 witnesses, NOT the release's exhaustive provider allowlist.
BASELINE = {
    'platform-clock-v1': ('platform.clock', []),
    'i2c-esp32s3-v2': ('i2c.bus', []),
    't5s3-usb-power-profile': ('board.power.bq25896.profile', []),
    'board-power-t5s3-v2': ('board.power.vbus',
                             ['i2c.bus', 'platform.clock', 'board.power.bq25896.profile']),
    'usb-controller-esp32s3': ('usb.controller', ['board.power.vbus']),
    'usb-host-v2': ('usb.host', ['usb.controller']),
    'usb-cdc-acm-v2': ('serial.port', ['usb.host']),
    'usb-cp210x-v2': ('serial.port', ['usb.host']),
    'usb-serial-witness': ('serial.port', ['usb.host']),
    'usb-ftdi': ('serial.port', ['usb.host']),
    'usb-stlink': ('debug.vendor.stlink', ['usb.host', 'platform.clock']),
    'usb-msp': ('debug.vendor.msp', ['usb.host', 'platform.clock']),
    'program-msp': ('program.msp', ['debug.vendor.msp']),
    'usb-hid': ('usb.hid', ['usb.host']),
    'usb-hid-keyboard': ('usb.hid.keyboard', ['usb.hid']),
    'usb-hid-gamepad': ('usb.hid.gamepad', ['usb.hid']),
    'usb-xinput-gamepad': ('usb.xinput.gamepad', ['usb.host', 'platform.clock']),
    'usb-ui-navigation': ('input.navigation',
                          ['usb.hid.keyboard', 'usb.hid.gamepad', 'usb.xinput.gamepad']),
}
EXPECTED_ABSOLUTE_POINTERS = {
    'usb-controller-esp32s3': {0x600c0000, 0x60039000, 0x60008000,
                               0x60038000, 0x60080000},
}


def mutation_rejected(elf: bytes, site: int, replacement: int) -> bool:
    data = bytearray(elf)
    hdr = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    shoff, shentsize, shnum = hdr[5], hdr[10], hdr[11]
    for i in range(shnum):
        section = struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize)
        virtual, file_offset, size = section[3], section[4], section[5]
        if virtual <= site and site + 4 <= virtual + size and section[1] != 8:
            struct.pack_into('<I', data, file_offset + site - virtual, replacement)
            with tempfile.TemporaryDirectory() as dirname:
                path = Path(dirname) / 'mutant.elf'
                path.write_bytes(data)
                observed = audit_loader_map(path)
                return bool(observed['unmapped_relative_values'])
    raise AssertionError('RELATIVE pointer source is not mapped')


def source_manifests():
    manifests = {}
    for source in sorted((ROOT / 'Drivers').glob('*/manifest.json')):
        metadata = json.loads(source.read_text(encoding='utf-8'))
        if metadata.get('type') != 'driver' or metadata.get('driver_abi') != 2:
            continue
        identity = metadata['id']
        assert identity not in manifests, identity
        manifests[identity] = metadata
    assert manifests, "No ABI-v2 source manifests found"
    missing = set(BASELINE) - set(manifests)
    assert not missing, f"Missing baseline provider IDs: {sorted(missing)}"
    return manifests


def run(identities=None):
    sources = source_manifests()
    selected = set(identities) if identities else set(sources)
    assert selected and selected <= set(sources), selected
    catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
    assert catalog['schema'] == 1 and catalog['release']
    assert len(catalog['packages']) == len(sources) <= 64
    available = {}
    observed_ids = set()
    for record in catalog['packages']:
        identity = record['id']
        assert identity in sources and identity not in observed_ids, identity
        observed_ids.add(identity)
        source = sources[identity]
        folder = PACKAGES / identity
        manifest_bytes = (folder / '.package.json').read_bytes()
        assert len(manifest_bytes) <= 4096
        manifest = json.loads(manifest_bytes)
        assert set(manifest) == {'schema', 'kind', 'id', 'version', 'artifact',
                                 'architecture', 'min_runtime_api', 'entries', 'requires'}
        assert manifest['schema'] == 1 and manifest['kind'] == 'driver'
        assert record['kind'] == manifest['kind'] and record['id'] == manifest['id']
        assert record['version'] == manifest['version'] == source['version']
        assert record['artifact'] == manifest['artifact'] == 'driver.elf'
        assert record['architecture'] == manifest['architecture'] == source['architecture']
        assert manifest['min_runtime_api'] == 2
        cap, api = source['provides'][0]['capability'], source['provides'][0]['api']
        deps = [requirement['capability'] for requirement in source['requires']]
        if identity in BASELINE:
            assert (cap, deps) == BASELINE[identity]
        assert manifest['requires'] == [
            {'capability': item['capability'], 'min_api': item['api']}
            for item in source['requires']]
        for requirement in manifest['requires']:
            assert available.get(requirement['capability'], 0) >= requirement['min_api'], (
                identity, requirement)
        entries = manifest['entries']
        names = {e['name'] for e in entries}
        assert names == {'driver.elf', 'provider-abi.v1', 'privileged-imports.v1'}
        assert {path.name for path in folder.iterdir()} == names | {'.package.json'}
        for item in entries:
            name = item['name']
            body = (folder / name).read_bytes()
            assert len(body) == item['size_bytes'] and body
            assert hashlib.sha256(body).hexdigest() == item['sha256']
            assert item['executable'] == (name == 'driver.elf')
        archive_path = PACKAGES / record['archive']
        assert record['archive'].endswith('.rte.zip') and archive_path.is_file()
        archive = archive_path.read_bytes()
        assert record['size_bytes'] == len(archive)
        assert record['sha256'] == hashlib.sha256(archive).hexdigest()
        with ZipFile(archive_path) as bundled:
            assert bundled.testzip() is None
            assert set(bundled.namelist()) == names | {'.package.json'}
            for name in bundled.namelist():
                assert bundled.getinfo(name).compress_type == ZIP_STORED
                assert bundled.read(name) == (folder / name).read_bytes()
        elf_path = folder / 'driver.elf'
        elf = elf_path.read_bytes()
        assert elf[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(elf[16:18], 'little') == 3
        assert int.from_bytes(elf[18:20], 'little') == 94
        mapping = audit_loader_map(elf_path)
        assert mapping['relocations_examined'] > 0, identity
        assert not mapping['unmapped_relocations'], (identity, mapping['unmapped_relocations'])
        assert not mapping['unmapped_relative_values'], (identity, mapping['unmapped_relative_values'])
        assert not mapping['unmapped_executable_sections'], (identity, mapping['unmapped_executable_sections'])
        absolute = mapping['absolute_peripheral_relocations']
        if identity in EXPECTED_ABSOLUTE_POINTERS:
            assert {int(item['address'], 16) for item in absolute} == (
                EXPECTED_ABSOLUTE_POINTERS[identity]), identity
        if absolute:
            site = int(absolute[0]['offset'], 16)
            assert mutation_rejected(elf, site, 0x70000000), identity
            assert mutation_rejected(elf, site, 0x60004010), identity
        profile = (folder / 'provider-abi.v1').read_text(encoding='ascii')
        assert profile == f'os-cpu-abi=1\nprovides={cap}\napi={api}\n'
        imports_bytes = (folder / 'privileged-imports.v1').read_bytes()
        imports = extract_imports(elf_path)
        assert imports_bytes == encode_imports(imports), identity
        assert imports == sorted(set(imports))
        assert (BRIDGE in imports) == (cap == 'i2c.bus'), identity
        if cap == 'i2c.bus':
            assert not any(name.startswith(('i2c_', 'gpio_', 'periph_module_'))
                           for name in imports), imports
        available[cap] = max(available.get(cap, 0), api)
    assert observed_ids == set(sources)
    print(f'{len(observed_ids)} manifest-discovered ELF packages: ZIP/catalog CRC/SHA, '
          'imports, MMIO and dependencies PASS')

    # The compatibility bridge must discover serial providers dynamically from
    # the installed provider graph. A new serial.port package must therefore
    # be reachable without maintaining a second hard-coded ID table.
    bridge_source = (ROOT / 'src/native/NativeUsbBridge.cpp').read_text(encoding='utf-8')
    class_bridge_source = (
        ROOT / 'src/native/NativeUsbClassBridge.cpp').read_text(encoding='utf-8')
    assert 'nativeUsbClassBindNextInstalled(&cursor, &faulted)' in bridge_source, (
        'USB serial provider iteration missing from compatibility bridge')
    assert re.search(
        r'installedClass\.select\(\s*"serial\.port"\s*,\s*1\s*,\s*cursor\s*,',
        class_bridge_source), 'USB serial provider discovery is not capability-driven'
    expected_serial = {
        identity for identity, source in sources.items()
        if source['provides'][0]['capability'] == 'serial.port'
    }
    assert expected_serial, 'no serial.port provider packages discovered'

    print(f'{len(selected)} selected USB ELF packages: MMIO, negative mutations, imports, SHA-256 PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ids', nargs='+',
                        help='verify only these discovered driver package IDs')
    args = parser.parse_args()
    synthetic = unittest.defaultTestLoader.loadTestsFromTestCase(ProviderDiscoveryTest)
    if not unittest.TextTestRunner(verbosity=2).run(synthetic).wasSuccessful():
        sys.exit(1)
    try:
        run(args.ids)
    except (AssertionError, OSError, ValueError, KeyError) as exc:
        traceback.print_exc()
        print(f'USB stack package verification FAIL: {exc}', file=sys.stderr)
        sys.exit(1)
