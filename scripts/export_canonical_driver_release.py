#!/usr/bin/env python3
"""Stage deterministic ordinary package ZIPs and one generic release catalog.

The exporter must enforce the same identity bounds as the embedded parser:
otherwise a successful build can publish an archive the device cannot admit.
No release is created or published by this script.
"""
from __future__ import annotations

import argparse
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
                     architecture: object, artifact: object, payload: object = "executable") -> bool:
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
        ((payload == 'resources' and kind == 'service' and artifact is None) or
         (payload == 'executable' and isinstance(artifact, str) and 4 < len(artifact) < 128 and
          artifact[0] != '_' and ENTRY.fullmatch(artifact) is not None and
          artifact.endswith('.elf') and '..' not in artifact))
    )


def export(identities: set[str] | None = None, output: Path | None = None, kind_filter: str | None = None) -> None:
    if kind_filter is not None and kind_filter not in KINDS:
        raise ValueError('invalid export kind')
    target = output if output is not None else TARGET
    if not SOURCE.is_dir():
        raise FileNotFoundError(f'ordinary package source missing: {SOURCE}')
    if target.exists() and (not target.is_dir() or any(target.iterdir())):
        raise FileExistsError(f'release export directory is not empty: {target}')
    release = os.environ.get('RISC_PACKAGE_RELEASE', 'unpublished-build')
    if not isinstance(release, str) or not 0 < len(release) < 64 or not re.fullmatch(
            r'[A-Za-z0-9._-]+', release) or '..' in release:
        raise ValueError('invalid immutable release identifier')

    requested = identities
    if requested is not None and (
            not requested or any(not isinstance(identity, str) or
                                 not 0 < len(identity) < 64 or
                                 SAFE.fullmatch(identity) is None
                                 for identity in requested)):
        raise ValueError(f'invalid requested package IDs: {list(requested)}')

    staged = []
    seen = set()
    selected = set()
    observed_kinds = set()
    for directory in sorted(SOURCE.iterdir()):
        if not directory.is_dir() or directory.is_symlink():
            continue
        if requested is not None and directory.name not in requested:
            continue
        manifest_path = directory / '.package.json'
        if not manifest_path.is_file() or manifest_path.is_symlink():
            continue
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        kind, identity, version, architecture, artifact = (
            manifest.get('kind'), manifest.get('id'), manifest.get('version'),
            manifest.get('architecture'), manifest.get('artifact'))
        if (type(manifest.get('schema')) is not int or manifest['schema'] not in (1, 2, 3) or
                not runtime_identity(kind, identity, version, architecture, artifact, manifest.get("payload", "executable")) or
                directory.name != identity):
            raise ValueError(f'invalid package identity or schema: {directory}')
        if kind_filter is not None and kind != kind_filter:
            if requested is not None and identity in requested:
                raise ValueError('requested package staged with wrong kind')
            continue
        if kind == "driver" and identity == "usb-cdc-acm-v2":
            raise ValueError("retired CDC alias cannot be exported")
        key = (kind, identity, architecture)
        if key in seen:
            raise ValueError(f'duplicate package identity/target: {key}')
        seen.add(key)
        selected.add(identity)
        observed_kinds.add(kind)
        # The shared packer validates declared leaves, implicit parent
        # directories, links and exact bounded inventory for both schemas.
        name = f'{kind}-{identity}-{version}-{architecture}.rte.zip'
        if len(name) >= 160:
            raise ValueError(f'archive name exceeds device catalog bound: {name}')
        archive = pack_directory(directory)
        if not archive or len(archive) > MAX_ARCHIVE_BYTES:
            raise ValueError(f'archive exceeds export bound: {identity}')
        staged.append((name, archive, catalog_row(directory, name, archive)))
        if len(staged) > MAX_PACKAGES:
            raise ValueError('catalog exceeds device package limit')
    if requested is not None and selected != requested:
        raise ValueError(f'package source missing requested IDs: {sorted(requested - selected)}')
    if not staged:
        raise ValueError('no ordinary packages found')
    if (release != 'unpublished-build' and requested is None and kind_filter is None and
            not {'application', 'driver'} <= observed_kinds):
        raise ValueError(f'release catalog missing required U1 kinds: {observed_kinds}')

    target.mkdir(parents=True, exist_ok=True)
    for name, archive, _ in staged:
        (target / name).write_bytes(archive)
    index = {'schema': 1, 'release': release,
             'packages': [row for _, _, row in staged]}
    (target / 'package-catalog.json').write_text(
        json.dumps(index, indent=2, ensure_ascii=True) + '\n', encoding='utf-8')
    print(f'Exported {len(staged)} bundled packages + package-catalog.json; no release published.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ids', nargs='+', help='export only these canonical package IDs')
    parser.add_argument('--output', type=Path, help='isolated product artifact directory')
    parser.add_argument('--kind', choices=sorted(KINDS), help='require this package kind')
    args = parser.parse_args()
    export(set(args.ids) if args.ids else None, args.output, args.kind)
