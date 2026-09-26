"""Validate a native app sidecar before building/publishing its ELF."""
import json
import re
from pathlib import Path


_CAPABILITY = re.compile(r'[a-z][a-z0-9._-]{0,38}', re.ASCII)
_API_MIN = re.compile(r'>=([1-9][0-9]*)', re.ASCII)


def _validate_version(path, field, value, label):
    if not isinstance(value, str) or len(value.encode('utf-8')) >= 32:
        raise ValueError(f'{path}: invalid {field}')
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', value) or any(int(n) > 65535 for n in value.split('.')):
        raise ValueError(f'{path}: {label} must be major.minor.patch')


def _validate_capabilities(path, data):
    # Keep the host release validator in sync with the bounded firmware parser.
    seen = set()
    for field in ('requires', 'optional'):
        if field not in data or data[field] is None:
            continue  # Legacy sidecars have no capability declarations.
        values = data[field]
        if not isinstance(values, list) or len(values) > 6:
            raise ValueError(f'{path}: {field} must be an array of at most six capabilities')
        for entry in values:
            if not isinstance(entry, dict) or set(entry) != {'capability', 'api'}:
                raise ValueError(f'{path}: invalid {field} requirement')
            capability = entry['capability']
            api = entry['api']
            match = _API_MIN.fullmatch(api) if isinstance(api, str) else None
            if (not isinstance(capability, str) or not _CAPABILITY.fullmatch(capability)
                    or not match or int(match[1]) > 65535 or capability in seen):
                raise ValueError(f'{path}: invalid, duplicate or unsupported {field} requirement')
            seen.add(capability)


def _validate_file_types(path, data):
    if 'supported_file_types' not in data:
        return
    values = data['supported_file_types']
    if not isinstance(values, list) or len(values) > 12:
        raise ValueError(f'{path}: supported_file_types must be an array of at most twelve extensions')
    seen = set()
    for value in values:
        if (not isinstance(value, str) or not re.fullmatch(r'\.[a-z0-9]{1,14}', value)
                or value in seen):
            raise ValueError(f'{path}: invalid or duplicate supported file type')
        seen.add(value)


def validate_manifest(source, output):
    path = Path(source).with_suffix('.json')
    raw = path.read_bytes()
    if not raw or len(raw) > 2048 or b'\x00' in raw:
        raise ValueError(f'{path}: manifest exceeds runtime limits')
    data = json.loads(raw.decode('utf-8'))
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
    _validate_capabilities(path, data)
    _validate_file_types(path, data)
    return path
