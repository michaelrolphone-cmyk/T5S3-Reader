#!/usr/bin/env python3
"""Create a verified release-index record from built product artifacts."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import tempfile
import io
import zipfile
from pathlib import Path

if __package__:
    from .pack_rte_zip import pack_directory, MAX_ENTRY_BYTES, MAX_TOTAL_BYTES, MAX_ARCHIVE_BYTES
    from .generate_provider_package_inputs_v1 import canonical_manifest
    from .update_release_index import validate_bundle_manifest
else:
    from pack_rte_zip import pack_directory, MAX_ENTRY_BYTES, MAX_TOTAL_BYTES, MAX_ARCHIVE_BYTES
    from generate_provider_package_inputs_v1 import canonical_manifest
    from update_release_index import validate_bundle_manifest

REPOSITORY = "michaelrolphone-cmyk/T5S3-Reader"
VERSION_RE = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")


def digest(path: Path) -> tuple[int, str]:
    data = path.read_bytes()
    if not data:
        raise ValueError(f"release asset is empty: {path}")
    return len(data), hashlib.sha256(data).hexdigest()


def bounded_file(root: Path, path: Path, maximum: int) -> bytes:
    """Read a regular build artifact without following any output symlink."""
    relative = path.relative_to(root)
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"release input contains a symlink: {current}")
    if not path.is_file() or not 0 < path.stat().st_size <= maximum:
        raise ValueError(f"release input missing, empty or oversized: {path}")
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    if not data or len(data) > maximum:
        raise ValueError(f"release input exceeded its read bound: {path}")
    return data


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def bounded_json(root: Path, path: Path, maximum: int):
    return json.loads(bounded_file(root, path, maximum), object_pairs_hook=unique_object)


def driver_bundle(identity: str, version: str, root: Path) -> tuple[dict, Path, bytes]:
    """Verify source identity, ordinary bundle, exported bytes and generic catalog.

    The canonical packer reconstructs a bounded private snapshot of the bundle.
    Exact equality validates ZIP topology/CRC/contents without decompressing or
    extracting unvalidated paths, and excludes appended/undeclared members.
    This verifies integrity, not publisher trust or runtime import permission.
    """
    if not re.fullmatch(r'[a-z0-9](?:[a-z0-9_-]{0,61}[a-z0-9])?', identity):
        raise ValueError('unsafe canonical driver identity')
    sources = []
    for index, path in enumerate(sorted((root / 'Drivers').glob('*/manifest.json'))):
        if index >= 64:
            raise ValueError('driver source inventory exceeds bound')
        source = bounded_json(root, path, 4096)
        if not isinstance(source, dict):
            raise ValueError('driver source manifest must be an object')
        if source.get('type') == 'driver' and source.get('driver_abi') == 2 and source.get('id') == identity:
            canonical_manifest(path)
            sources.append(source)
    if len(sources) != 1 or sources[0].get('version') != version:
        raise ValueError('source driver identity/version is missing, duplicated or stale')
    source = sources[0]
    architecture = source['architecture']
    name = f'driver-{identity}-{version}-{architecture}.rte.zip'
    archive_path = root / 'dist/release-packages' / name
    archive = bounded_file(root, archive_path, MAX_ARCHIVE_BYTES)
    try:
        with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
            infos = zipped.infolist()
            if not 2 <= len(infos) <= 17:
                raise ValueError('driver ZIP entry count exceeds bound')
            names = set()
            total = 0
            for info in infos:
                entry_name = info.filename
                if (entry_name in names or info.is_dir() or
                        (entry_name != '.package.json' and not re.fullmatch(
                            r'[a-z0-9](?:[a-z0-9._-]{0,125}[a-z0-9])?', entry_name)) or
                        '..' in entry_name or info.flag_bits or info.compress_type != zipfile.ZIP_STORED or
                        info.compress_size != info.file_size or info.external_attr or
                        info.internal_attr or info.extra or info.comment or
                        not 0 < info.file_size <= (4096 if entry_name == '.package.json' else MAX_ENTRY_BYTES)):
                    raise ValueError('driver ZIP has noncanonical, unsafe or oversized entries')
                names.add(entry_name)
                total += info.file_size
            if total > MAX_TOTAL_BYTES or '.package.json' not in names:
                raise ValueError('driver ZIP content/manifest bound violated')
            raw = zipped.read('.package.json')  # Stored and bounded above; CRC is checked.
            if not raw.isascii() or b'\\' in raw:
                raise ValueError('driver manifest contains unsupported JSON escapes or non-ASCII bytes')
            manifest = json.loads(raw, object_pairs_hook=unique_object)
            validate_bundle_manifest(manifest, identity, version, architecture)
            if manifest['artifact'] != source['file_name'] or manifest['requires'] != [
                    {'capability': item['capability'], 'min_api': item['api']}
                    for item in source['requires']]:
                raise ValueError('bundled driver artifact/dependencies differ from source manifest')
            if names != {'.package.json'} | {entry['name'] for entry in manifest['entries']}:
                raise ValueError('driver ZIP has missing or undeclared files')
            with tempfile.TemporaryDirectory(prefix='rte-record-') as temporary:
                snapshot = Path(temporary)
                (snapshot / '.package.json').write_bytes(raw)
                for entry in manifest['entries']:
                    payload = zipped.read(entry['name'])
                    if len(payload) != entry['size_bytes'] or hashlib.sha256(payload).hexdigest() != entry['sha256']:
                        raise ValueError('driver ZIP entry size/SHA-256 mismatch')
                    if entry['name'] == 'provider-abi.v1':
                        capability, api = source['provides'][0]['capability'], source['provides'][0]['api']
                        expected = f'os-cpu-abi=1\nprovides={capability}\napi={api}\n'.encode('ascii')
                        if payload != expected:
                            raise ValueError('bundled provider ABI differs from source manifest')
                    (snapshot / entry['name']).write_bytes(payload)
                expected_archive = pack_directory(snapshot)
            if archive != expected_archive:
                raise ValueError('exported driver ZIP differs from canonical stored format')
    except (zipfile.BadZipFile, RuntimeError, KeyError) as exc:
        raise ValueError('driver ZIP is corrupt or incomplete') from exc
    catalog = bounded_json(root, root / 'dist/release-packages/package-catalog.json', 32768)
    if not isinstance(catalog, dict) or type(catalog.get('schema')) is not int or catalog['schema'] != 1:
        raise ValueError('generic package catalog schema is invalid')
    release = catalog.get('release')
    if not isinstance(release, str) or not re.fullmatch(r'[A-Za-z0-9._-]{1,63}', release) or '..' in release:
        raise ValueError('generic package catalog release is invalid')
    packages = catalog.get('packages')
    if not isinstance(packages, list) or not 1 <= len(packages) <= 64:
        raise ValueError('generic package catalog exceeds bounds')
    matches = [item for item in packages if isinstance(item, dict) and
               item.get('kind') == 'driver' and item.get('id') == identity]
    expected_row = {'kind': 'driver', 'id': identity, 'version': version,
                    'artifact': manifest['artifact'], 'architecture': architecture,
                    'archive': name, 'size_bytes': len(archive),
                    'sha256': hashlib.sha256(archive).hexdigest()}
    if len(matches) != 1 or matches[0] != expected_row:
        raise ValueError('generic catalog does not uniquely match the verified driver ZIP')
    return manifest, archive_path, archive


def build_record(product: str, identity: str, version: str, root: Path) -> dict:
    if product not in ("firmware", "apps", "drivers"):
        raise ValueError("unsupported release product")
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        raise ValueError("version must be numeric MAJOR.MINOR.PATCH")
    prefix = {"firmware": "firmware", "apps": "app", "drivers": "driver"}[product]
    tag = f"{prefix}-v{version}" if product == "firmware" else f"{prefix}-{identity}-v{version}"
    if product == "firmware":
        asset_path = root / "dist/firmware-t5s3-pro.bin"
        manifest = None
    elif product == "apps":
        manifest_path = root / "dist/apps" / f"{identity}.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("file_name") != f"{identity}.elf" or manifest.get("version") != version:
            raise ValueError("built app manifest identity/version does not match the request")
        asset_path = root / "dist/apps" / f"{identity}.elf"
    else:
        manifest, asset_path, archive = driver_bundle(identity, version, root)
    size, sha256 = (len(archive), hashlib.sha256(archive).hexdigest()) if product == "drivers" else digest(asset_path)
    asset = asset_path.name
    record = {
        "id": identity if product != "firmware" else None,
        "version": version,
        "tag": tag,
        "asset": asset,
        "url": f"https://github.com/{REPOSITORY}/releases/download/{tag}/{asset}",
        "size": size,
        "sha256": sha256,
    }
    if product != "firmware":
        record["manifest"] = manifest
    else:
        record.pop("id")
    if product == "drivers":
        record["format"] = "rte.zip"
        record["architecture"] = manifest["architecture"]
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", required=True, choices=("firmware", "apps", "drivers"))
    parser.add_argument("--id", default="")
    parser.add_argument("--version", required=True)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.product != "firmware" and not args.id:
        parser.error("--id is required for apps and drivers")
    record = build_record(args.product, args.id, args.version, args.root)
    args.output.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Verified {args.product} release record for {args.id or 'firmware'}")


if __name__ == "__main__":
    main()
