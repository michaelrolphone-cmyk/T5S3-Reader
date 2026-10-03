#!/usr/bin/env python3
"""Freeze firmware and ordinary SD packages for X4/T5; local artifacts only.

Historical CLI name retained for existing build jobs. Never constructs an
internal flash driver image or accesses a device. No-SD targets use their own
separate packaging path.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile
from pack_rte_zip import pack_directory

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


def pack_sd_tree(tree, archive):
    # GitHub upload-artifact excludes dotfiles in directory trees by default.
    # Freeze the ENTIRE tree inside one ordinary visible ZIP, then round-trip
    # all declared files. Do not rely on caller-specific uploader flags.
    expected=inventory(tree)
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_STORED) as output:
        for name,record in expected.items():
            info=zipfile.ZipInfo('sdcard/'+name,date_time=(1980,1,1,0,0,0))
            info.external_attr=0o100644 << 16
            output.writestr(info,(tree/name).read_bytes())
    with zipfile.ZipFile(archive) as check:
        if set(check.namelist()) != {'sdcard/'+name for name in expected}:
            raise ValueError('SD archive inventory mismatch')
        for name,record in expected.items():
            data=check.read('sdcard/'+name)
            if dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest()) != record:
                raise ValueError('SD archive round-trip mismatch')
    return digest(archive)


def build(tool, output, source_sha, board="xteink-x4-pro"):
    profiles = {"xteink-x4-pro": "x4-independent-packages", "t5s3-pro": "t5s3-independent-packages"}
    if board not in profiles: raise ValueError("Unsupported board profile")
    if not re.fullmatch(r'[0-9a-f]{40}', source_sha):
        raise ValueError('Invalid source SHA')
    observed = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if observed != source_sha:
        raise ValueError('Checkout does not match source SHA')
    packages = ROOT/'dist'/profiles[board]
    expected = inventory(packages/'sdcard')
    firmware = ROOT/'.pio/build'/board/'firmware.bin'
    payload = firmware.read_bytes()
    if not payload or payload[0] != 0xe9 or len(payload) > 0x640000 or ('RISCRTE_BOARD_ID:'+board).encode() not in payload:
        raise ValueError('Invalid paired board firmware')
    # Refuse stale output rather than silently mix runs.
    output.mkdir(parents=True, exist_ok=False)
    shutil.copytree(packages/'sdcard',output/'sdcard')
    if inventory(output/'sdcard') != expected:
        raise ValueError('SD package snapshot differs from staged files')
    shutil.copyfile(firmware, output/'firmware.bin')
    shutil.copyfile(ROOT/'partitions.csv', output/'partitions.csv')
    archive_records = json.loads((packages/'artifacts.json').read_text())
    (output/'packages').mkdir()
    for record in archive_records:
        name = record['file']
        if Path(name).name != name or digest(packages/name) != {k: record[k] for k in ('bytes', 'sha256')}:
            raise ValueError('Package archive custody mismatch')
        identity=record['id']
        if not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,62}',identity):
            raise ValueError('Invalid installed package ID')
        packed=pack_directory(output/'sdcard/Drivers'/identity)
        if dict(bytes=len(packed),sha256=hashlib.sha256(packed).hexdigest()) != digest(packages/name):
            raise ValueError('Installed SD generation differs from its ordinary archive')
        if digest(output/'sdcard/Packages/Inbox'/name) != digest(packages/name):
            raise ValueError('SD inbox archive differs from approved package')
        shutil.copyfile(packages/name, output/'packages'/name)
    sd_archive=output/'sdcard.zip'
    packed=pack_sd_tree(output/'sdcard',sd_archive)
    # A partially uploaded raw tree must never be mistaken for the install input.
    shutil.rmtree(output/'sdcard')
    manifest = dict(schema=2, board=board, source_sha=source_sha,
                    provisioning_authorized=False, driver_medium='sd',
                    firmware=dict(file='firmware.bin', offset=0x10000, **digest(output/'firmware.bin')),
                    sd_root='sdcard', sd_archive=dict(file=sd_archive.name,**packed), partition_table=digest(output/'partitions.csv'),
                    files=expected, packages=archive_records)
    (output/'deployment.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (output/'README.txt').write_text(
        'Software artifact only; no device validation or provisioning performed.\n'
        'Firmware requires ordinary driver generations and the board profile on SD.\n'
        'Verify sdcard.zip, then stage its sdcard/ paths with checked installation; preserve unrelated data.\n'
        'Active driver generations refuse replacement; no hot-update claim.\n'
        'No internal-flash driver image is supplied or required. Do not erase old flash contents.\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, help='Legacy compatibility argument; unused for SD artifacts')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-sha', required=True)
    parser.add_argument('--board', choices=['xteink-x4-pro', 't5s3-pro'], default='xteink-x4-pro')
    args = parser.parse_args()
    build(args.tool, args.output, args.source_sha, args.board)
