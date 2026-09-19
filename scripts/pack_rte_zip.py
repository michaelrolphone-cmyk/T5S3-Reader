#!/usr/bin/env python3
"""Pack one schema-1 ordinary package into a bounded stored .rte.zip.

Archive files and sizes MUST fit the firmware bootstrap decoder before release.
ZIP CRC is transport only; ordinary .package.json SHA-256 is file integrity,
not publisher trust. No network, ZIP service or signing dependency.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

MAX_ENTRIES = 17  # kMaxPackageEntries + one .package.json
MAX_NAME_BYTES = 127
MAX_ENTRY_BYTES = 1024 * 1024
MAX_TOTAL_BYTES = 4 * 1024 * 1024
MAX_ARCHIVE_BYTES = MAX_TOTAL_BYTES + 65536
MAX_MANIFEST_BYTES = 4096
SAFE_NAME = re.compile(r'[A-Za-z0-9_][A-Za-z0-9_.-]*\Z')


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def put_local(name: bytes, payload: bytes, crc: int) -> bytes:
    return struct.pack('<IHHHHHIIIHH', 0x04034B50, 20, 0, 0, 0, 0,
                       crc, len(payload), len(payload), len(name), 0) + name + payload


def put_central(name: bytes, payload: bytes, crc: int, offset: int) -> bytes:
    return struct.pack('<IHHHHHHIIIHHHHHII', 0x02014B50, 20, 20, 0, 0,
                       0, 0, crc, len(payload), len(payload), len(name),
                       0, 0, 0, 0, 0, offset) + name


def pack_directory(source: Path) -> bytes:
    manifest_path = source / '.package.json'
    if not source.is_dir() or manifest_path.is_symlink() or not manifest_path.is_file():
        raise ValueError('ordinary package manifest missing or is a symlink')
    raw_manifest = manifest_path.read_bytes()
    if not 2 <= len(raw_manifest) <= MAX_MANIFEST_BYTES:
        raise ValueError('manifest exceeds device parser bound')
    manifest = json.loads(raw_manifest)
    if manifest.get('schema') != 1:
        raise ValueError('ordinary schema 1 required')
    entries = manifest['entries']
    if not isinstance(entries, list) or not 1 <= len(entries) < MAX_ENTRIES:
        raise ValueError('declared inventory exceeds firmware ZIP entry limit')
    files = [('.package.json', raw_manifest)]
    declared = {'.package.json'}
    total_bytes = len(raw_manifest)
    for item in entries:
        if not isinstance(item, dict):
            raise ValueError('invalid entry descriptor')
        name = item.get('name')
        if (not isinstance(name, str) or len(name) > MAX_NAME_BYTES or
                not SAFE_NAME.fullmatch(name) or name in ('.', '..') or
                name.casefold() in {old.casefold() for old in declared}):
            raise ValueError(f'unsafe, duplicate or nested schema-1 entry: {name!r}')
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'missing or nonregular entry: {name}')
        payload = path.read_bytes()
        if (not payload or len(payload) > MAX_ENTRY_BYTES or
                len(payload) != item['size_bytes']):
            raise ValueError(f'device bound or manifest size mismatch: {name}')
        total_bytes += len(payload)
        if total_bytes > MAX_TOTAL_BYTES:
            raise ValueError('archive content exceeds device bootstrap limit')
        digest = hashlib.sha256(payload).hexdigest()
        if digest != item['sha256']:
            raise ValueError(f'sha256 mismatch: {name}')
        files.append((name, payload))
        declared.add(name)
    if {entry.name for entry in source.iterdir()} != declared:
        raise ValueError('source contains undeclared or missing package entries')
    locals_ = bytearray()
    centrals = bytearray()
    offset = 0
    for name, payload in files:
        encoded = name.encode('ascii')
        crc = crc32(payload)
        local = put_local(encoded, payload, crc)
        centrals.extend(put_central(encoded, payload, crc, offset))
        locals_.extend(local)
        offset += len(local)
    eocd = struct.pack('<IHHHHIIH', 0x06054B50, 0, 0, len(files),
                       len(files), len(centrals), len(locals_), 0)
    archive = bytes(locals_) + bytes(centrals) + eocd
    if len(archive) > MAX_ARCHIVE_BYTES:
        raise ValueError('archive exceeds firmware ZIP file-length bound')
    return archive


def catalog_row(source: Path, archive_name: str, archive: bytes) -> dict:
    manifest = json.loads((source / '.package.json').read_text(encoding='utf-8'))
    return {
        'kind': manifest['kind'],
        'id': manifest['id'],
        'version': manifest['version'],
        'artifact': manifest['artifact'],
        'architecture': manifest['architecture'],
        'archive': archive_name,
        'size_bytes': len(archive),
        'sha256': hashlib.sha256(archive).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='ordinary package directory')
    parser.add_argument('output', type=Path, help='destination .rte.zip')
    args = parser.parse_args()
    archive = pack_directory(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(archive)
    print(json.dumps(catalog_row(args.source, args.output.name, archive)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
