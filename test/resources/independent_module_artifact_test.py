#!/usr/bin/env python3
"""Validate actual service/provider ZIPs through existing independent delivery.

Offline only. The current build supplies dist/packages; the shared exporter,
record/index producer and real C++ catalog consumer form the round trip.
"""
import json
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_canonical_driver_release as exporter
from build_release_record import build_record
from update_release_index import update_index, serialize_index
from generate_provider_package_inputs_v1 import canonical_manifest
from package_catalog_roundtrip_test import CPP


def verify(root):
    index = {'schema': 1, 'firmware': None, 'apps': [], 'drivers': []}
    records = []
    previous = exporter.SOURCE
    try:
        exporter.SOURCE = root / 'dist/packages'
        for kind, folder in [('service', 'Services'), ('provider', 'Providers')]:
            sources = []
            for path in sorted((root / folder).glob('*/manifest.json')):
                canonical_manifest(path)
                manifest = json.loads(path.read_text())
                if manifest['type'] != kind:
                    raise ValueError('source module kind mismatch')
                sources.append(manifest)
            if not sources:
                continue
            exporter.export({item['id'] for item in sources}, root / f'dist/release-{kind}-packages', kind)
            for source in sources:
                record = build_record(kind + 's', source['id'], source['version'], root)
                index = update_index(index, kind + 's', record)
                records.append(record)
    finally:
        exporter.SOURCE = previous
    if not records:
        raise ValueError('no actual independent service/provider artifact checked')
    with tempfile.TemporaryDirectory(prefix='real-independent-modules-') as temporary:
        path = Path(temporary)
        (path / 'roundtrip.cpp').write_text(CPP)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(ROOT / 'src'), str(path / 'roundtrip.cpp'), '-o', str(path / 'roundtrip')], check=True)
        (path / 'index.json').write_text(serialize_index(index))
        result = subprocess.run([str(path / 'roundtrip'), str(path / 'index.json')],
                                check=True, capture_output=True, text=True)
        if set(result.stdout.splitlines()) != {record['url'] for record in records}:
            raise ValueError('runtime immutable module locator mismatch')
    print(f'PASS: {len(records)} actual service/provider ZIP records reach exact runtime URLs; no publication')
    return records


if __name__ == '__main__':
    records = verify(ROOT)
    source_sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    (ROOT / 'dist/independent-module-record-report.json').write_text(json.dumps({
        'source_sha': source_sha, 'published': False, 'records': records}, indent=2) + '\n')
