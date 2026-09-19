#!/usr/bin/env python3
"""Verify real USB provider packages, imports, pointers and ZIP catalog."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
from zipfile import ZipFile, ZIP_STORED

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_privileged_imports_v1 import extract_imports, encode_imports
from verify_provider_relocation_map import audit_loader_map
PACKAGES = ROOT / 'dist/packages'
CATALOG = PACKAGES / 'package-catalog.json'
BRIDGE = 'risc_fw_i2c_transact_v1'
EXPECTED = {
    'platform-clock-v1': ('platform.clock', []),
    'i2c-esp32s3-v2': ('i2c.bus', []),
    'board-power-t5s3-v2': ('board.power.vbus', ['i2c.bus', 'platform.clock']),
    'usb-controller-esp32s3': ('usb.controller', ['board.power.vbus']),
    'usb-host-v2': ('usb.host', ['usb.controller']),
    'usb-cdc-acm-v2': ('serial.port', ['usb.host']),
    'usb-cp210x-v2': ('serial.port', ['usb.host']),
}
EXPECTED_ABSOLUTE_POINTERS = {
    'usb-controller-esp32s3': {0x600c0000, 0x60039000, 0x60008000,
                               0x60038000, 0x60080000},
}


def mutation_rejected(elf: bytes, site: int, replacement: int) -> bool:
    """Change a mapped RELATIVE pointer value, leaving its relocation intact."""
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


def run():
    catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
    assert catalog['schema'] == 1 and catalog['release']
    assert len(catalog['packages']) == len(EXPECTED)
    available = {}
    observed_ids = set()
    for record in catalog['packages']:
        identity = record['id']
        assert identity in EXPECTED and identity not in observed_ids, identity
        observed_ids.add(identity)
        folder = PACKAGES / identity
        manifest_bytes = (folder / '.package.json').read_bytes()
        assert len(manifest_bytes) <= 4096
        manifest = json.loads(manifest_bytes)
        assert set(manifest) == {'schema', 'kind', 'id', 'version', 'artifact',
                                 'architecture', 'min_runtime_api', 'entries', 'requires'}
        assert manifest['schema'] == 1 and manifest['kind'] == 'driver'
        assert record['kind'] == manifest['kind'] and record['id'] == manifest['id']
        assert record['version'] == manifest['version']
        assert record['artifact'] == manifest['artifact'] == 'driver.elf'
        assert record['architecture'] == manifest['architecture'] == 'xtensa-esp32s3'
        assert manifest['min_runtime_api'] == 2
        cap, deps = EXPECTED[identity]
        assert [r['capability'] for r in manifest['requires']] == deps
        for requirement in manifest['requires']:
            assert requirement == {'capability': requirement['capability'], 'min_api': 1}
            assert available.get(requirement['capability']) == 1, (identity, requirement)
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
        assert {int(item['address'], 16) for item in absolute} == EXPECTED_ABSOLUTE_POINTERS.get(identity, set()), identity
        if absolute:
            site = int(absolute[0]['offset'], 16)
            assert mutation_rejected(elf, site, 0x70000000), identity
            assert mutation_rejected(elf, site, 0x60004010), identity
        profile = (folder / 'provider-abi.v1').read_text(encoding='ascii')
        assert profile == f'os-cpu-abi=1\nprovides={cap}\napi=1\n'
        imports_bytes = (folder / 'privileged-imports.v1').read_bytes()
        imports = extract_imports(elf_path)
        assert imports_bytes == encode_imports(imports), identity
        assert imports == sorted(set(imports))
        assert (BRIDGE in imports) == (identity == 'i2c-esp32s3-v2'), identity
        if identity == 'i2c-esp32s3-v2':
            assert not any(name.startswith(('i2c_', 'gpio_', 'periph_module_'))
                           for name in imports), imports
        available[cap] = 1
    assert observed_ids == set(EXPECTED)
    print('USB ELF packages: ZIP/catalog CRC/SHA, imports, MMIO and dependencies PASS')


if __name__ == '__main__':
    try:
        run()
    except (AssertionError, OSError, ValueError, KeyError) as exc:
        print(f'USB stack package verification FAIL: {exc}', file=sys.stderr)
        sys.exit(1)
