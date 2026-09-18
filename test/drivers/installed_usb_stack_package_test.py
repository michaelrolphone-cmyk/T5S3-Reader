#!/usr/bin/env python3
"""Verify real built USB provider packages, including exact ELF imports."""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_privileged_imports_v1 import extract_imports, encode_imports
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
        assert manifest['id'] == id and manifest['version'] == '0.1.0'
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
        elf = (folder / 'driver.elf').read_bytes()
        assert elf[:7] == b'\x7fELF\x01\x01\x01' and int.from_bytes(elf[16:18], 'little') == 3
        assert int.from_bytes(elf[18:20], 'little') == 94
        profile = (folder / 'provider-abi.v1').read_text(encoding='ascii')
        assert profile == f'os-cpu-abi=1\nprovides={cap}\napi=1\n'
        imports_bytes = (folder / 'privileged-imports.v1').read_bytes()
        names = extract_imports(folder / 'driver.elf')
        assert imports_bytes == encode_imports(names), id
        assert names == sorted(set(names))
        assert {e['name'] for e in record['files']} == {'driver.elf',
            'provider-abi.v1', 'privileged-imports.v1', '.package.json'}
        available[cap] = 1
    assert observed_ids == set(EXPECTED)
    print('Seven real USB provider ELF packages: exact imports, SHA-256, ABI and dependency order PASS')


if __name__ == '__main__':
    try:
        run()
    except (AssertionError, OSError, ValueError, KeyError) as exc:
        print(f'USB stack package verification FAIL: {exc}', file=sys.stderr)
        sys.exit(1)
