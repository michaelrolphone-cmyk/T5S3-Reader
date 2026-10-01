#!/usr/bin/env python3
"""Offline release-plan/artifact preflight. Never tags, publishes or edits an index."""
from __future__ import annotations
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re

if __package__:
    from .build_release_record import build_record, bounded_json
    from .publish_updated_packages import release_assets
    from .update_release_index import validate_record, version_tuple
else:
    from build_release_record import build_record, bounded_json
    from publish_updated_packages import release_assets
    from update_release_index import validate_record, version_tuple

LIMITS = {'firmware': 1, 'apps': 128, 'drivers': 64}
MAX_ASSET_BYTES = 128 * 1024 * 1024  # Includes unstripped firmware debug ELF.


def validate_plan(plan):
    if not isinstance(plan, list) or len(plan) > sum(LIMITS.values()):
        raise ValueError('release plan exceeds product count limits')
    seen, counts = set(), dict.fromkeys(LIMITS, 0)
    for item in plan:
        if not isinstance(item, dict) or set(item) != {'product', 'id', 'version'}:
            raise ValueError('release plan requires exact product/id/version fields')
        product, identity = item['product'], item['id']
        if product not in LIMITS or not isinstance(identity, str):
            raise ValueError('invalid release product or identity')
        if ((product == 'firmware' and identity != '') or
                (product != 'firmware' and (not re.fullmatch(r'[a-z0-9][a-z0-9._-]{0,63}', identity) or '..' in identity))):
            raise ValueError('unsafe release plan identity')
        version_tuple(item['version'])
        key = (product, identity)
        if key in seen:
            raise ValueError('duplicate release plan product identity')
        seen.add(key)
        counts[product] += 1
        if counts[product] > LIMITS[product]:
            raise ValueError('release plan exceeds product count limits')
    return plan


def asset_digest(root, path):
    current = root
    for part in path.relative_to(root).parts:
        current /= part
        if current.is_symlink():
            raise ValueError('release artifact path contains a symlink')
    if not path.is_file() or not 0 < path.stat().st_size <= MAX_ASSET_BYTES:
        raise ValueError('release artifact is missing, empty or oversized')
    count, digest = 0, hashlib.sha256()
    with path.open('rb') as stream:
        while True:
            data = stream.read(65536)
            if not data:
                break
            count += len(data)
            if count > MAX_ASSET_BYTES:
                raise ValueError('release artifact grew beyond its bound')
            digest.update(data)
    return count, digest.hexdigest()


def verify_plan(root: Path, plan, product: str | None = None):
    validate_plan(plan)  # Validate the complete plan even for a filtered job.
    if product is not None and product not in LIMITS:
        raise ValueError('unknown product filter')
    records = []
    for item in plan:
        if product is not None and item['product'] != product:
            continue
        assets = release_assets(root, item['product'], item['id'], item['version'])
        if item['product'] == 'apps':
            bounded_json(root, root / 'dist/apps' / f"{item['id']}.json", 2048)
        hashes = {path.name: asset_digest(root, path) for path in assets}
        if len(hashes) != len(assets):
            raise ValueError('duplicate release artifact basename')
        record = validate_record(item['product'], build_record(
            item['product'], item['id'], item['version'], root))
        if hashes.get(record['asset']) != (record['size'], record['sha256']):
            raise ValueError('release record does not match its staged artifact')
        if item['product'] == 'firmware':
            config = configparser.ConfigParser(interpolation=None, strict=False,
                                                inline_comment_prefixes=(';', '#'))
            config.read(root / 'platformio.ini')
            if config.get('riscrte', 'version') != item['version']:
                raise ValueError('firmware source version differs from plan')
            alias = f"riscrte_lilygo_t5s3_{item['version']}-app.bin"
            if hashes.get(alias) != hashes[record['asset']]:
                raise ValueError('firmware OTA aliases differ')
        elif item['product'] == 'apps':
            manifest = record['manifest']
            if ('size_bytes' in manifest and manifest['size_bytes'] != record['size']) or (
                    'sha256' in manifest and manifest['sha256'] != record['sha256']):
                raise ValueError('app sidecar integrity differs from its ELF')
        records.append(record)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', required=True, type=Path)
    parser.add_argument('--product', choices=tuple(LIMITS))
    parser.add_argument('--root', type=Path, default=Path('.'))
    args = parser.parse_args()
    # The plan can be an Actions artifact outside the checkout, but is bounded.
    plan = bounded_json(args.plan.parent, args.plan, 65536)
    records = verify_plan(args.root, plan, args.product)
    print(f'Verified {len(records)} planned release records/assets offline; no publication or index changes.')


if __name__ == '__main__':
    main()
