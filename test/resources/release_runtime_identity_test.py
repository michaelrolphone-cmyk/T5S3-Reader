#!/usr/bin/env python3
"""Release exporter must reject identities the embedded catalog cannot parse."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from export_canonical_driver_release import runtime_identity


def permitted(identity='usb-class-extra', version='1.2.3',
              architecture='xtensa-esp32s3', artifact='driver.elf', kind='driver'):
    return runtime_identity(kind, identity, version, architecture, artifact)


assert permitted()
assert permitted(kind='application', identity='app_store', artifact='app_store.elf')
assert permitted(kind='service') and permitted(kind='provider')
assert permitted(identity='a' * 63)
assert not permitted(identity='a' * 64)
assert not permitted(identity='Usb-class')
assert not permitted(identity='../outside')
assert not permitted(version='4294967296.0.0')
assert permitted(version='4294967295.0.0')
assert not permitted(version='1.' + '2' * 32 + '.0')
assert not permitted(architecture='a' * 32)
assert not permitted(artifact='a' * 124 + '.elf')
assert not permitted(artifact='../driver.elf')
assert not permitted(artifact='driver.bin')
assert not permitted(kind='unsupported')
print('Release runtime identity bounds: four kinds, widths, versions and safe ELF names PASS')
