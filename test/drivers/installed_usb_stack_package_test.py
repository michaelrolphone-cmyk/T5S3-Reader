#!/usr/bin/env python3
"""Verify real built USB provider packages, including exact ELF pointer values."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_privileged_imports_v1 import extract_imports, encode_imports
from verify_provider_relocation_map import audit_loader_map
PACKAGES = ROOT / 'dist/packages'
CATALOG = PACKAGES / 'usb-provider-catalog.json'
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
    'i2c-esp32s3-v2': {0x60013000, 0x60027000, 0x60004000},
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
    assert catalog['schema'] == 1 and len(catalog['packages']) == len(EXPECTED)
    available = {}
    observed_ids = set()
    for record in catalog['packages']:
        id = record['id']
        assert id in EXPECTED and id not in observed_ids, id
        observed_ids.add(id)
        folder = PACKAGES / id
        manifest_bytes = (folder / '.package.json').read_bytes()
        assert len(manifest_bytes) <= 4096
        manifest = json.loads(manifest_bytes)
        assert set(manifest) == {'schema', 'kind', 'id', 'version', 'artifact',
                                 'architecture', 'min_runtime_api', 'entries', 'requires'}
        assert manifest['schema'] == 1 and manifest['kind'] == 'driver'
        expected_version = '0.1.1' if id == 'i2c-esp32s3-v2' else '0.1.0'
        assert manifest['id'] == id and manifest['version'] == expected_version
        assert record['version'] == expected_version
        assert manifest['artifact'] == 'driver.elf'
        assert manifest['architecture'] == 'xtensa-esp32s3'
        assert manifest['min_runtime_api'] == 2
        cap, deps = EXPECTED[id]
        assert record['capability'] == cap and record['api'] == 1
        assert [r['capability'] for r in manifest['requires']] == deps
        for requirement in manifest['requires']:
            assert requirement == {'capability': requirement['capability'], 'min_api': 1}
            assert available.get(requirement['capability']) == 1, (id, requirement)
        entries = manifest['entries']
        names = {e['name'] for e in entries}
        assert names == {'driver.elf', 'provider-abi.v1', 'privileged-imports.v1'}
        assert {path.name for path in folder.iterdir()} == names | {'.package.json'}
        for entry in entries:
            name = entry['name']
            body = (folder / name).read_bytes()
            assert len(body) == entry['size_bytes'] and body
            assert hashlib.sha256(body).hexdigest() == entry['sha256']
            assert entry['executable'] == (name == 'driver.elf')
        elf_path = folder / 'driver.elf'
        elf = elf_path.read_bytes()
        assert elf[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(elf[16:18], 'little') == 3
        assert int.from_bytes(elf[18:20], 'little') == 94
        mapping = audit_loader_map(elf_path)
        assert mapping['relocations_examined'] > 0, id
        assert not mapping['unmapped_relocations'], (id, mapping['unmapped_relocations'])
        assert not mapping['unmapped_relative_values'], (id, mapping['unmapped_relative_values'])
        assert not mapping['unmapped_executable_sections'], (id, mapping['unmapped_executable_sections'])
        absolute = mapping['absolute_peripheral_relocations']
        assert {int(record['address'], 16) for record in absolute} == EXPECTED_ABSOLUTE_POINTERS.get(id, set()), id
        if absolute:
            site = int(absolute[0]['offset'], 16)
            # Physical address outside the architecture's privileged MMIO range.
            assert mutation_rejected(elf, site, 0x70000000), id
            # Still inside the MMIO window, but not declared SHN_ABS by this ELF.
            assert mutation_rejected(elf, site, 0x60004010), id
        profile = (folder / 'provider-abi.v1').read_text(encoding='ascii')
        assert profile == f'os-cpu-abi=1\nprovides={cap}\napi=1\n'
        imports_bytes = (folder / 'privileged-imports.v1').read_bytes()
        names = extract_imports(elf_path)
        assert imports_bytes == encode_imports(names), id
        assert names == sorted(set(names))
        assert {e['name'] for e in record['files']} == {'driver.elf',
            'provider-abi.v1', 'privileged-imports.v1', '.package.json'}
        available[cap] = 1
    assert observed_ids == set(EXPECTED)
    print('Seven USB ELF packages: exact pointer values, ABS MMIO, negative mutations, imports, SHA-256 and dependencies PASS')


if __name__ == '__main__':
    try:
        run()
    except (AssertionError, OSError, ValueError, KeyError) as exc:
        print(f'USB stack package verification FAIL: {exc}', file=sys.stderr)
        sys.exit(1)
