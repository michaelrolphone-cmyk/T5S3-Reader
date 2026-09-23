#!/usr/bin/env python3
"""Flatten all canonical physical/class ELF packages into unique release assets.

No publication occurs here. Asset names match NativeOnlineDriverInstall:
  <id>--package.json, <id>--driver.elf,
  <id>--provider-abi.v1, <id>--privileged-imports.v1
The branch's legacy source index is usb-provider-catalog.json; the separate
U1 milestone will replace this with generic package-catalog.json.
"""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'dist/packages'
TARGET = ROOT / 'dist/release-packages'
FILES = ('.package.json', 'driver.elf', 'provider-abi.v1', 'privileged-imports.v1')
EXPECTED_IDS = {
    'platform-clock-v1', 'i2c-esp32s3-v2', 'board-power-t5s3-v2',
    'usb-controller-esp32s3', 'usb-host-v2', 'usb-cdc-acm-v2',
    'usb-cp210x-v2', 'usb-ch34x-v2', 'usb-hid',
    'usb-hid-keyboard', 'usb-hid-gamepad', 'usb-xinput-gamepad',
}


def export() -> None:
    index_path = SOURCE / 'usb-provider-catalog.json'
    index = json.loads(index_path.read_text(encoding='utf-8'))
    packages = index.get('packages')
    if index.get('schema') != 1 or not isinstance(packages, list) or \
       {p['id'] for p in packages} != EXPECTED_IDS or len(packages) != len(EXPECTED_IDS):
        raise ValueError('canonical driver index is absent or incomplete')
    if TARGET.exists() and any(TARGET.iterdir()):
        raise FileExistsError(f'release export directory is not empty: {TARGET}')
    TARGET.mkdir(parents=True, exist_ok=True)
    observed = set()
    for package in packages:
        identity = package['id']
        if (identity in observed or not isinstance(identity, str) or
                not identity or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in identity)):
            raise ValueError(f'invalid/duplicate package identity: {identity!r}')
        observed.add(identity)
        indexed = {file['name']: file for file in package['files']}
        if set(indexed) != set(FILES):
            raise ValueError(f'incomplete declared file inventory: {identity}')
        for name in FILES:
            source = SOURCE / identity / name
            contents = source.read_bytes()
            declaration = indexed[name]
            if (not contents or declaration['size_bytes'] != len(contents) or
                    declaration['sha256'] != hashlib.sha256(contents).hexdigest()):
                raise ValueError(f'payload not equal to package inventory: {identity}/{name}')
            asset = f'{identity}--{name.lstrip(".")}'
            shutil.copyfile(source, TARGET / asset)
    shutil.copyfile(index_path, TARGET / 'usb-provider-catalog.json')
    print(f'Exported {len(packages)} complete canonical driver packages; no release published.')


if __name__ == '__main__':
    export()
