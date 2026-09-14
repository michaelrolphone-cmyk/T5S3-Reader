"""Validate a native app sidecar before building/publishing its ELF."""
import json
import re
from pathlib import Path


def _validate_version(path, field, value, label):
    if not isinstance(value, str) or len(value.encode('utf-8')) >= 32:
        raise ValueError(f'{path}: invalid {field}')
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', value) or any(int(n) > 65535 for n in value.split('.')):
        raise ValueError(f'{path}: {label} must be major.minor.patch')


def validate_manifest(source, output):
    path = Path(source).with_suffix('.json')
    data = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(data, dict):
        raise ValueError(f'{path}: expected an object')
    for field, limit in [('display_name', 96), ('file_name', 128),
                         ('min_firmware_version', 32), ('version', 32), ('icon', 24)]:
        value = data.get(field)
        if not isinstance(value, str) or not value or len(value.encode('utf-8')) >= limit or any(ord(c) < 32 for c in value):
            raise ValueError(f'{path}: invalid {field}')
    _validate_version(path, 'version', data['version'], 'app version')
    _validate_version(path, 'min_firmware_version', data['min_firmware_version'], 'minimum firmware')
    name = data['file_name']
    if not re.fullmatch(r'[A-Za-z0-9_.-]+\.elf', name) or '..' in name or name != Path(output).name:
        raise ValueError(f'{path}: file_name must match output ELF basename')
    icon = re.fullmatch(r'(solid|regular):([0-9a-fA-F]{4,6})', data['icon'])
    if not icon or not 0 < int(icon[2], 16) <= 0x10ffff or 0xd800 <= int(icon[2], 16) <= 0xdfff:
        raise ValueError(f'{path}: invalid Classic icon')
    return path
