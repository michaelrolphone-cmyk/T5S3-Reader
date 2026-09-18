#!/usr/bin/env python3
"""Assemble already-linked hardware-owning ELFs into installable ordinary packages.

Run after the seven actual provider build/probe commands, not against mock
objects or the old firmware USB driver. Each folder is directly consumable by
Package Manager at /Packages/Inbox/<id>. No firmware flashing or publishing.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import sys

from generate_provider_package_inputs_v1 import prepare as provider_inputs

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'dist/experimental'
DESTINATION = ROOT / 'dist/packages'
# Dependency order doubles as a direct-install order for an initially empty SD.
DRIVERS = (
    ('platform-clock-v1', 'platform_clock_v1', 'platform-clock-v1', 'driver.elf'),
    ('i2c-esp32s3-v2', 'i2c_esp32s3_v2', 'i2c-esp32s3-v2', 'driver.elf'),
    ('board-power-t5s3-v2', 'board_power_t5s3_v2', 'usb-board-power-t5s3-v2', 'driver.elf'),
    ('usb-controller-esp32s3', 'usb_controller_esp32s3', 'usb-controller-esp32s3',
     'controller-link-experiment.elf'),
    ('usb-host-v2', 'usb_host_v2', 'usb-host-v2', 'driver.elf'),
    ('usb-cdc-acm-v2', 'usb_cdc_v2', 'usb-cdc-acm-v2', 'driver.elf'),
    ('usb-cp210x-v2', 'usb_cp210x_v2', 'usb-cp210x-v2', 'driver.elf'),
)


def entry(path: Path, executable: bool) -> dict:
    payload = path.read_bytes()
    if not payload:
        raise ValueError(f'empty package entry: {path}')
    return {'name': path.name, 'size_bytes': len(payload),
            'sha256': hashlib.sha256(payload).hexdigest(),
            'executable': executable}


def build() -> list[dict]:
    from generate_provider_package_inputs_v1 import canonical_manifest
    DESTINATION.mkdir(parents=True, exist_ok=True)
    catalog = []
    for identity, source_name, output_name, elf_name in DRIVERS:
        source = ROOT / 'Drivers' / source_name / 'manifest.json'
        metadata = json.loads(source.read_text(encoding='utf-8'))
        if metadata.get('id') != identity or metadata.get('architecture') != 'xtensa-esp32s3':
            raise ValueError(f'unexpected source identity: {source}')
        capability, api = canonical_manifest(source)
        version = metadata.get('version', '0.1.0')
        if version != '0.1.0':
            raise ValueError(f'unknown package version for {identity}: {version}')
        elf = SOURCE / output_name / elf_name
        if not elf.is_file() or elf.stat().st_size < 52:
            raise FileNotFoundError(f'actual linked provider ELF missing: {elf}')
        target = DESTINATION / identity
        target.mkdir(parents=True, exist_ok=True)
        executable = target / 'driver.elf'
        shutil.copyfile(elf, executable)
        provider_inputs(executable, source, target)
        dependencies = [{'capability': required['capability'], 'min_api': required['api']}
                        for required in metadata['requires']]
        entries = [entry(target / name, name == 'driver.elf') for name in
                   ('driver.elf', 'provider-abi.v1', 'privileged-imports.v1')]
        package = {'schema': 1, 'kind': 'driver', 'id': identity,
                   'version': version, 'artifact': 'driver.elf',
                   'architecture': 'xtensa-esp32s3', 'min_runtime_api': 2,
                   'entries': entries, 'requires': dependencies}
        encoded = (json.dumps(package, separators=(',', ':'), ensure_ascii=True) + '\n').encode('ascii')
        if len(encoded) > 4096:
            raise ValueError(f'manifest exceeds device parser bound: {identity}')
        (target / '.package.json').write_bytes(encoded)
        catalog.append({'id': identity, 'version': version, 'capability': capability,
                        'api': api, 'requires': dependencies,
                        'files': [entry(target / name, name == 'driver.elf') for name in
                                  ('.package.json', 'driver.elf', 'provider-abi.v1',
                                   'privileged-imports.v1')]})
        print(f'Installable: {identity} -> {target} ({capability}@{api})', flush=True)
    (DESTINATION / 'usb-provider-catalog.json').write_text(
        json.dumps({'schema': 1, 'packages': catalog}, indent=2) + '\n',
        encoding='utf-8')
    print('Seven canonical packages assembled; no ELF activated or firmware flashed.', flush=True)
    return catalog


if __name__ == '__main__':
    try:
        build()
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f'Provider package build FAILED: {exc}', file=sys.stderr)
        sys.exit(1)
