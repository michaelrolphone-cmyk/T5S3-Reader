#!/usr/bin/env python3
"""Test canonical package inputs using actual physical PIC ELFs, not stubs."""
import copy
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from generate_provider_package_inputs_v1 import (  # noqa: E402
    canonical_manifest, prepare)
from generate_privileged_imports_v1 import extract_imports  # noqa: E402

SOURCES = (
    ('Drivers/usb_controller_esp32s3/manifest.json',
     'dist/experimental/usb-controller-esp32s3/controller-link-experiment.elf',
     'usb.controller'),
    ('Drivers/i2c_esp32s3_v2/manifest.json',
     'dist/experimental/i2c-esp32s3-v2/driver.elf', 'i2c.bus'),
    ('Drivers/platform_clock_v1/manifest.json',
     'dist/experimental/platform-clock-v1/driver.elf', 'platform.clock'),
)


def run() -> None:
    with tempfile.TemporaryDirectory(prefix='risc-provider-inputs-') as temporary:
        root = Path(temporary)
        for index, (manifest_path, elf_path, expected_capability) in enumerate(SOURCES):
            manifest = ROOT / manifest_path
            elf = ROOT / elf_path
            directory = root / str(index)
            abi, imports = prepare(elf, manifest, directory)
            names = extract_imports(elf)
            assert canonical_manifest(manifest) == (expected_capability, 1)
            assert abi.read_bytes() == (
                f'os-cpu-abi=1\nprovides={expected_capability}\napi=1\n'.encode('ascii'))
            assert imports.read_bytes() == (
                ''.join(name + '\n' for name in names).encode('ascii'))
            assert 1 <= len(names) <= 128

        source = json.loads((ROOT / SOURCES[0][0]).read_text())
        bad = root / 'bad.json'

        def reject(label: str, value: dict) -> None:
            bad.write_text(json.dumps(value))
            try:
                canonical_manifest(bad)
            except ValueError:
                return
            raise AssertionError(f'provider manifest accepted {label}')

        scenarios = [
            ('wrong ABI', {'driver_abi': 1}),
            ('bogus type', {'type': 'application'}),
            ('architecture mismatch', {'architecture': 'riscv32'}),
            ('incorrect executable', {'file_name': 'other.elf'}),
            ('uppercase identity', {'id': 'USB-controller'}),
            ('two providers', {'provides': source['provides'] * 2}),
            ('empty providers', {'provides': []}),
            ('uppercase capability', {'provides': [{'capability': 'USB.controller', 'api': 1}]}),
            ('zero provided API', {'provides': [{'capability': 'usb.controller', 'api': 0}]}),
            ('boolean provided API', {'provides': [{'capability': 'usb.controller', 'api': True}]}),
            ('overflow API', {'provides': [{'capability': 'usb.controller', 'api': 4294967296}]}),
            ('duplicate dependencies', {'requires': source['requires'] * 2}),
            ('invalid dependency API', {'requires': [{'capability': 'i2c.bus', 'api': 0}]}),
            ('too many dependencies', {'requires': [
                {'capability': f'cap.{n}', 'api': 1} for n in range(17)]}),
        ]
        for label, mutation in scenarios:
            invalid = copy.deepcopy(source)
            invalid.update(mutation)
            reject(label, invalid)
        bad.write_text('{"type":"driver","type":"driver"}')
        try:
            canonical_manifest(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('duplicate JSON manifest key accepted')
        print(f'Physical provider package inputs: 3 real ELFs and '
              f'{len(scenarios) + 1} malformed metadata rejections PASS')


if __name__ == '__main__':
    run()
