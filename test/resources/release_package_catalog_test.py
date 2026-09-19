#!/usr/bin/env python3
"""Validate one immutable ordinary catalog and its exact release ZIP asset set.

This is a prepublication contract check, not publisher authentication. ZIP
CRC-32 and manifest SHA-256 are integrity checks; runtime authorization and
physical provider ownership are separate from release packaging.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[2]
DIST = ROOT / 'dist/release-packages'
CATALOG = DIST / 'package-catalog.json'
KINDS = {'application', 'driver', 'service', 'provider'}
SEMVER = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')
SAFE_ID = re.compile(r'[a-z0-9](?:[a-z0-9_-]*[a-z0-9])?\Z')
SAFE_TAG = re.compile(r'[A-Za-z0-9](?:[A-Za-z0-9._-]*[A-Za-z0-9])?\Z')
MAX_ROWS = 64
MAX_ENTRY = 1024 * 1024
MAX_TOTAL = 4 * 1024 * 1024
MAX_ARCHIVE = MAX_TOTAL + 8192


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    assert DIST.is_dir() and not DIST.is_symlink()
    assert CATALOG.is_file() and not CATALOG.is_symlink()
    raw = CATALOG.read_bytes()
    assert 2 <= len(raw) <= 32768 and raw.isascii()
    document = json.loads(raw)
    assert document.get('schema') == 1
    release = document.get('release')
    assert isinstance(release, str) and len(release) < 64 and SAFE_TAG.fullmatch(release)
    expected_tag = os.getenv('RISC_PACKAGE_RELEASE')
    assert not expected_tag or release == expected_tag, 'release catalog points at another tag'
    packages = document.get('packages')
    assert isinstance(packages, list) and 1 <= len(packages) <= MAX_ROWS
    seen = set()
    kinds = set()
    archives = set()
    for row in packages:
        kind, identity = row['kind'], row['id']
        version, architecture = row['version'], row['architecture']
        archive_name = row['archive']
        assert kind in KINDS and SAFE_ID.fullmatch(identity)
        assert SEMVER.fullmatch(version) and architecture == 'xtensa-esp32s3'
        assert isinstance(archive_name, str) and len(archive_name) < 160
        assert archive_name == f'{kind}-{identity}-{version}-{architecture}.rte.zip'
        assert '/' not in archive_name and '\\' not in archive_name and '..' not in archive_name
        key = (kind, identity, architecture)
        assert key not in seen
        seen.add(key)
        assert archive_name.casefold() not in archives
        archives.add(archive_name.casefold())
        kinds.add(kind)
        archive = DIST / archive_name
        assert archive.is_file() and not archive.is_symlink()
        payload = archive.read_bytes()
        assert 22 <= len(payload) <= MAX_ARCHIVE and len(payload) == row['size_bytes']
        assert digest(payload) == row['sha256']
        with zipfile.ZipFile(archive, 'r') as zipped:
            members = zipped.infolist()
            names = [member.filename for member in members]
            assert len(members) <= 17 and names.count('.package.json') == 1
            assert len(names) == len(set(name.casefold() for name in names))
            assert all(not member.is_dir() and member.compress_type == zipfile.ZIP_STORED and
                       not (member.external_attr >> 16 & 0o170000 == 0o120000) and
                       member.file_size <= MAX_ENTRY for member in members)
            assert sum(member.file_size for member in members) <= MAX_TOTAL
            raw_manifest = zipped.read('.package.json')
            assert 2 <= len(raw_manifest) <= 4096
            manifest = json.loads(raw_manifest.decode('ascii'))
            assert manifest['schema'] == 1
            assert (manifest['kind'], manifest['id'], manifest['version'],
                    manifest['architecture'], manifest['artifact']) == (
                    kind, identity, version, architecture, row['artifact'])
            declared = {'.package.json'}
            for entry in manifest['entries']:
                name = entry['name']
                assert name not in declared and name in names
                data = zipped.read(name)  # Checks ZIP CRC before SHA validation.
                assert data and len(data) == entry['size_bytes'] and digest(data) == entry['sha256']
                declared.add(name)
            assert set(names) == declared
    assert {'application', 'driver'} <= kinds
    actual = {p.name.casefold() for p in DIST.iterdir() if p.is_file()}
    assert actual == archives | {'package-catalog.json'}, 'undeclared or missing release asset'
    assert all(p.is_file() and not p.is_symlink() for p in DIST.iterdir()), 'nonregular release asset'
    print(f"validated {len(packages)} immutable ordinary ZIPs for {release}: {sorted(kinds)}")


if __name__ == '__main__':
    main()
