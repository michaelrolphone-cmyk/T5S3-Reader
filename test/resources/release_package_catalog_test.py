#!/usr/bin/env python3
"""Release-side contract test for the generic ordinary package catalog."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[2]
DIST = ROOT / 'dist/release-packages'
CATALOG = DIST / 'package-catalog.json'
KINDS = {'application', 'driver', 'service', 'provider'}
SEMVER = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')


def main() -> None:
    document = json.loads(CATALOG.read_text(encoding='utf-8'))
    assert document.get('schema') == 1
    assert isinstance(document.get('release'), str) and document['release']
    packages = document.get('packages')
    assert isinstance(packages, list) and packages
    seen = set()
    kinds = set()
    for row in packages:
        kind = row['kind']
        identity = row['id']
        version = row['version']
        archive_name = row['archive']
        assert kind in KINDS
        assert SEMVER.fullmatch(version)
        assert archive_name.endswith('.rte.zip') and '/' not in archive_name and '\\' not in archive_name
        key = (kind, identity, row['architecture'])
        assert key not in seen
        seen.add(key)
        kinds.add(kind)
        archive = DIST / archive_name
        payload = archive.read_bytes()
        assert len(payload) == row['size_bytes']
        assert hashlib.sha256(payload).hexdigest() == row['sha256']
        with zipfile.ZipFile(archive, 'r') as zipped:
            names = zipped.namelist()
            assert names.count('.package.json') == 1
            manifest = json.loads(zipped.read('.package.json').decode('ascii'))
            assert manifest['schema'] == 1
            assert manifest['kind'] == kind
            assert manifest['id'] == identity
            assert manifest['version'] == version
            assert manifest['architecture'] == row['architecture']
            assert manifest['artifact'] == row['artifact']
            declared = {'.package.json'} | {entry['name'] for entry in manifest['entries']}
            assert set(names) == declared
    # U1's normal online surfaces need both applications and hardware drivers
    # in the same immutable release index. Services/providers may be absent in
    # an early release, but the catalog format remains four-kind.
    assert 'application' in kinds
    assert 'driver' in kinds
    print(f"validated {len(packages)} generic release packages: {sorted(kinds)}")


if __name__ == '__main__':
    main()
