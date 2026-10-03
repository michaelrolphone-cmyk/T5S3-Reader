#!/usr/bin/env python3
"""Build and round-trip a separately provisioned X4 module-store artifact.

This command only writes local files. It does not discover or access devices.
The output explicitly requires separate approval before replacing the existing
data partition; the app-only hardware controller must not consume this bundle.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    data = path.read_bytes()
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def inventory(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('Module store must not contain symlinks')
        if path.is_file():
            result[path.relative_to(root).as_posix()] = digest(path)
    if not result or len(result) > 128:
        raise ValueError('Invalid bounded module-store inventory')
    return result


def build(tool, output, source_sha, board="xteink-x4-pro"):
    profiles = {"xteink-x4-pro": "x4-independent-packages", "t5s3-pro": "t5s3-independent-packages"}
    if board not in profiles: raise ValueError("Unsupported board profile")
    if not re.fullmatch(r'[0-9a-f]{40}', source_sha):
        raise ValueError('Invalid source SHA')
    observed = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if observed != source_sha:
        raise ValueError('Checkout does not match source SHA')
    # Derive geometry from this Reader checkout, never another port's layout.
    rows = [line.split(',') for line in (ROOT/'partitions.csv').read_text().splitlines()
            if line.strip() and not line.lstrip().startswith('#')]
    stores = [row for row in rows if row[0].strip() == 'spiffs']
    if len(stores) != 1:
        raise ValueError('Expected one existing module-store partition')
    offset, size = (int(stores[0][i].strip(), 0) for i in (3, 4))
    if (offset, size) != (0xc90000, 0x360000):
        raise ValueError('Partition layout changed; review required')
    packages = ROOT/'dist'/profiles[board]
    expected = inventory(packages/'bootfs')
    firmware = ROOT/'.pio/build'/board/'firmware.bin'
    payload = firmware.read_bytes()
    if not payload or payload[0] != 0xe9 or len(payload) > 0x640000 or ('RISCRTE_BOARD_ID:'+board).encode() not in payload:
        raise ValueError('Invalid paired board firmware')
    # Refuse stale output rather than silently mix runs.
    output.mkdir(parents=True, exist_ok=False)
    image = output/'module-store.bin'
    geometry = ['-b', '4096', '-p', '256', '-s', str(size)]
    subprocess.run([str(tool), '-c', str(packages/'bootfs'), *geometry, str(image)], check=True, timeout=120)
    if image.stat().st_size != size:
        raise ValueError('Unexpected module-store image length')
    with tempfile.TemporaryDirectory() as temporary:
        extracted = Path(temporary)/'extracted'
        extracted.mkdir()
        subprocess.run([str(tool), '-u', str(extracted), *geometry, str(image)], check=True, timeout=120)
        if inventory(extracted) != expected:
            raise ValueError('Module-store image round-trip differs from package files')
    shutil.copyfile(firmware, output/'firmware.bin')
    shutil.copyfile(ROOT/'partitions.csv', output/'partitions.csv')
    archive_records = json.loads((packages/'artifacts.json').read_text())
    (output/'packages').mkdir()
    for record in archive_records:
        name = record['file']
        if Path(name).name != name or digest(packages/name) != {k: record[k] for k in ('bytes', 'sha256')}:
            raise ValueError('Package archive custody mismatch')
        shutil.copyfile(packages/name, output/'packages'/name)
    manifest = dict(schema=1, board=board, source_sha=source_sha,
                    provisioning_authorized=False, prior_partition_contents='unknown; whole region would be replaced',
                    firmware=dict(file='firmware.bin', offset=0x10000, **digest(output/'firmware.bin')),
                    module_store=dict(file=image.name, offset=offset, **digest(image)),
                    partition_table=digest(output/'partitions.csv'), files=expected, packages=archive_records)
    (output/'deployment.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (output/'README.txt').write_text(
        'Software artifact only; no device validation or provisioning performed.\n'
        'Firmware requires the matching external module store.\n'
        'Do not give this bundle to the app-only automatic controller.\n'
        'Provisioning replaces the entire existing data partition; prior contents are unknown.\n'
        'Review/backup of that region and separate approval are required before provisioning.\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-sha', required=True)
    parser.add_argument('--board', choices=['xteink-x4-pro', 't5s3-pro'], default='xteink-x4-pro')
    args = parser.parse_args()
    build(args.tool, args.output, args.source_sha, args.board)
