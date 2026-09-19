#!/usr/bin/env python3
"""Stage one deterministic .rte.zip per ordinary package and a generic catalog.

No publication occurs here. This script deliberately does not consume the old
USB-only catalog or assume a fixed count, kind, version, or set of loose assets.
The caller supplies RISC_PACKAGE_RELEASE for the immutable release identity.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re

from pack_rte_zip import catalog_row, pack_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'dist/packages'
TARGET = ROOT / 'dist/release-packages'
KINDS = frozenset(('application', 'driver', 'service', 'provider'))
SAFE = re.compile(r'[a-z0-9][a-z0-9-]*\Z')
VERSION = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')
ARCH = re.compile(r'[a-z0-9][a-z0-9-]*\Z')
ENTRY = re.compile(r'[A-Za-z0-9_][A-Za-z0-9_.-]*\Z')
MAX_PACKAGES = 64  # Match PackageCatalog.h; fail rather than truncate.
MAX_ARCHIVE_BYTES = 32 * 1024 * 1024


def export() -> None:
    if not SOURCE.is_dir():
        raise FileNotFoundError(f'ordinary package source missing: {SOURCE}')
    if TARGET.exists() and (not TARGET.is_dir() or any(TARGET.iterdir())):
        raise FileExistsError(f'release export directory is not empty: {TARGET}')
    release = os.environ.get('RISC_PACKAGE_RELEASE', 'unpublished-build')
    if not release or len(release.encode('ascii')) >= 64 or not re.fullmatch(r'[A-Za-z0-9._-]+', release):
        raise ValueError('invalid immutable release identifier')

    staged = []
    identities = set()
    for directory in sorted(SOURCE.iterdir()):
        if not directory.is_dir() or directory.is_symlink():
            continue
        manifest_path = directory / '.package.json'
        if not manifest_path.is_file() or manifest_path.is_symlink():
            continue
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        kind, identity, version, architecture = (
            manifest.get('kind'), manifest.get('id'), manifest.get('version'),
            manifest.get('architecture'))
        if (manifest.get('schema') != 1 or kind not in KINDS or
                not isinstance(identity, str) or not SAFE.fullmatch(identity) or
                not isinstance(version, str) or not VERSION.fullmatch(version) or
                not isinstance(architecture, str) or not ARCH.fullmatch(architecture) or
                directory.name != identity):
            raise ValueError(f'invalid package identity or schema: {directory}')
        key = (kind, identity, architecture)
        if key in identities:
            raise ValueError(f'duplicate package identity/target: {key}')
        identities.add(key)
        declared = {'.package.json'}
        for item in manifest.get('entries', ()):
            name = item.get('name') if isinstance(item, dict) else None
            if not isinstance(name, str) or not ENTRY.fullmatch(name) or name in declared:
                raise ValueError(f'unsafe or duplicate schema-1 entry: {identity}/{name}')
            declared.add(name)
            path = directory / name
            if not path.is_file() or path.is_symlink():
                raise ValueError(f'missing or nonregular entry: {identity}/{name}')
        actual = {item.name for item in directory.iterdir()}
        if actual != declared:
            raise ValueError(f'undeclared or missing package files: {identity}: {actual ^ declared}')
        name = f'{kind}-{identity}-{version}-{architecture}.rte.zip'
        if len(name) >= 160:
            raise ValueError(f'archive name exceeds device catalog bound: {name}')
        archive = pack_directory(directory)  # Checks every declared size and SHA-256.
        if not archive or len(archive) > MAX_ARCHIVE_BYTES:
            raise ValueError(f'archive exceeds export bound: {identity}')
        staged.append((name, archive, catalog_row(directory, name, archive)))
        if len(staged) > MAX_PACKAGES:
            raise ValueError('catalog exceeds device package limit')
    if not staged:
        raise ValueError('no ordinary packages found')

    # Verify all sources before modifying the export destination. On a failed
    # export, never delete unrelated files or silently reuse old release assets.
    TARGET.mkdir(parents=True, exist_ok=True)
    for name, archive, _ in staged:
        (TARGET / name).write_bytes(archive)
    index = {'schema': 1, 'release': release,
             'packages': [row for _, _, row in staged]}
    (TARGET / 'package-catalog.json').write_text(
        json.dumps(index, indent=2, ensure_ascii=True) + '\n', encoding='utf-8')
    print(f'Exported {len(staged)} bundled packages + package-catalog.json; no release published.')


if __name__ == '__main__':
    export()
