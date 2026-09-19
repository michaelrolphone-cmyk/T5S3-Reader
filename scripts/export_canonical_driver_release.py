#!/usr/bin/env python3
"""Stage deterministic ordinary package ZIPs and one generic release catalog.

The exporter must enforce the same identity bounds as the embedded parser:
otherwise a successful build can publish an archive the device cannot admit.
No release is created or published by this script.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import re

from pack_rte_zip import catalog_row, pack_directory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'dist/packages'
TARGET = ROOT / 'dist/release-packages'
KINDS = frozenset(('application', 'driver', 'service', 'provider'))
SAFE = re.compile(r'[a-z0-9](?:[a-z0-9_-]*[a-z0-9])?\Z')
VERSION = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')
ARCH = re.compile(r'[a-z0-9][a-z0-9-]*\Z')
ENTRY = re.compile(r'[A-Za-z0-9_][A-Za-z0-9_.-]*\Z')
MAX_PACKAGES = 64
MAX_ARCHIVE_BYTES = 32 * 1024 * 1024
MAX_VERSION_COMPONENT = 0xffffffff


def runtime_identity(kind: object, identity: object, version: object,
                     architecture: object, artifact: object) -> bool:
    """Mirror fixed-size Identity and catalog fields before emitting assets."""
    return (
        isinstance(kind, str) and kind in KINDS and
        isinstance(identity, str) and 0 < len(identity) < 64 and
        SAFE.fullmatch(identity) is not None and
        isinstance(version, str) and len(version) < 32 and
        VERSION.fullmatch(version) is not None and
        all(int(component) <= MAX_VERSION_COMPONENT
            for component in version.split('.')) and
        isinstance(architecture, str) and 0 < len(architecture) < 32 and
        ARCH.fullmatch(architecture) is not None and
        isinstance(artifact, str) and 4 < len(artifact) < 128 and
        artifact[0] != '_' and ENTRY.fullmatch(artifact) is not None and
        artifact.endswith('.elf') and '..' not in artifact
    )


def export() -> None:
    if not SOURCE.is_dir():
        raise FileNotFoundError(f'ordinary package source missing: {SOURCE}')
    if TARGET.exists() and (not TARGET.is_dir() or any(TARGET.iterdir())):
        raise FileExistsError(f'release export directory is not empty: {TARGET}')
    release = os.environ.get('RISC_PACKAGE_RELEASE', 'unpublished-build')
    if not isinstance(release, str) or not 0 < len(release) < 64 or not re.fullmatch(
            r'[A-Za-z0-9._-]+', release) or '..' in release:
        raise ValueError('invalid immutable release identifier')

    staged = []
    identities = set()
    observed_kinds = set()
    for directory in sorted(SOURCE.iterdir()):
        if not directory.is_dir() or directory.is_symlink():
            continue
        manifest_path = directory / '.package.json'
        if not manifest_path.is_file() or manifest_path.is_symlink():
            continue
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        kind, identity, version, architecture, artifact = (
            manifest.get('kind'), manifest.get('id'), manifest.get('version'),
            manifest.get('architecture'), manifest.get('artifact'))
        if (manifest.get('schema') != 1 or
                not runtime_identity(kind, identity, version, architecture, artifact) or
                directory.name != identity):
            raise ValueError(f'invalid package identity or schema: {directory}')
        key = (kind, identity, architecture)
        if key in identities:
            raise ValueError(f'duplicate package identity/target: {key}')
        identities.add(key)
        observed_kinds.add(kind)
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
        archive = pack_directory(directory)
        if not archive or len(archive) > MAX_ARCHIVE_BYTES:
            raise ValueError(f'archive exceeds export bound: {identity}')
        staged.append((name, archive, catalog_row(directory, name, archive)))
        if len(staged) > MAX_PACKAGES:
            raise ValueError('catalog exceeds device package limit')
    if not staged:
        raise ValueError('no ordinary packages found')
    if release != 'unpublished-build' and not {'application', 'driver'} <= observed_kinds:
        raise ValueError(f'release catalog missing required U1 kinds: {observed_kinds}')

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
