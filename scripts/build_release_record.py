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
    from .app_manifest import validate_manifest, package_requirements
    from .package_resource_paths import safe_resource_path, validate_resource_imports
    from .package_resource_source import resource_source, resource_manifest
else:
    from pack_rte_zip import pack_directory, MAX_ENTRY_BYTES, MAX_TOTAL_BYTES, MAX_ARCHIVE_BYTES
    from generate_provider_package_inputs_v1 import canonical_manifest
    from update_release_index import validate_bundle_manifest
    from app_manifest import validate_manifest, package_requirements
    from package_resource_paths import safe_resource_path, validate_resource_imports
    from package_resource_source import resource_source, resource_manifest

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


def app_source(identity: str, version: str | None, root: Path) -> dict:
    if not re.fullmatch(r'[a-z0-9](?:[a-z0-9_-]{0,61}[a-z0-9])?', identity):
        raise ValueError('unsafe canonical application identity')
    sources = []
    for index, path in enumerate(sorted((root / 'Apps').rglob('*.json'))):
        if index >= 128:
            raise ValueError('application source inventory exceeds bound')
        source = bounded_json(root, path, 2048)
        if isinstance(source, dict) and source.get('file_name') == f'{identity}.elf':
            expected = str(path.relative_to(root / 'Apps').with_suffix('')).replace('/', '__') + '.elf'
            if source['file_name'] != expected:
                raise ValueError('application source path/identity mismatch')
            if not path.with_suffix('.c').is_file():
                raise ValueError('application source executable is missing')
            validate_manifest(path.with_suffix('.c'), Path(f'{identity}.elf'))
            sources.append(source)
    if len(sources) != 1 or (version is not None and sources[0].get('version') != version):
        raise ValueError('source application identity/version is missing, duplicated or stale')
    return sources[0]


def package_bundle(kind: str, identity: str, version: str, root: Path) -> tuple[dict, Path, bytes]:
    """Verify either kind through the same bounded canonical ZIP contract."""
    if not re.fullmatch(r'[a-z0-9](?:[a-z0-9_-]{0,61}[a-z0-9])?', identity):
        raise ValueError('unsafe canonical package identity')
    if kind in ('driver', 'service', 'provider'):
        sources = []
        source_paths = []
        source_root = {'driver': 'Drivers', 'service': 'Services', 'provider': 'Providers'}[kind]
        for index, path in enumerate(sorted((root / source_root).glob('*/manifest.json'))):
            if index >= 64:
                raise ValueError('driver source inventory exceeds bound')
            source = bounded_json(root, path, 4096)
            if not isinstance(source, dict):
                raise ValueError('driver source manifest must be an object')
            if source.get('type') == kind and (source.get('driver_abi') == 2 or source.get('payload') == 'resources') and source.get('id') == identity:
                if source.get('payload') == 'resources':
                    resource_source(path)
                else:
                    canonical_manifest(path)
                sources.append(source)
                source_paths.append(path)
        if len(sources) != 1 or sources[0].get('version') != version:
            raise ValueError('source driver identity/version is missing, duplicated or stale')
        source = sources[0]
        architecture = source['architecture']
        directory = root / ('dist/release-packages' if kind == 'driver' else f'dist/release-{kind}-packages')
        requirements = [{'capability': item['capability'], 'min_api': item['api']}
                        for item in source.get('requires', [])]
    elif kind == 'application':
        source = app_source(identity, version, root)
        architecture = 'xtensa-esp32s3'
        # Separate artifact roots prevent restored app/driver catalogs colliding.
        directory = root / 'dist/release-app-packages'
        requirements = package_requirements(source)
    else:
        raise ValueError('unsupported ordinary release product')
    name = f'{kind}-{identity}-{version}-{architecture}.rte.zip'
    archive_path = directory / name
    archive = bounded_file(root, archive_path, MAX_ARCHIVE_BYTES)
    try:
        with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
            infos = zipped.infolist()
            if not 2 <= len(infos) <= 17:
                raise ValueError('package ZIP entry count exceeds bound')
            names = set()
            total = 0
            for info in infos:
                entry_name = info.filename
                if (entry_name in names or info.is_dir() or
                        (entry_name != '.package.json' and not safe_resource_path(entry_name)) or
                        '..' in entry_name or info.flag_bits or info.compress_type != zipfile.ZIP_STORED or
                        info.compress_size != info.file_size or info.external_attr or
                        info.internal_attr or info.extra or info.comment or
                        not 0 < info.file_size <= (4096 if entry_name == '.package.json' else MAX_ENTRY_BYTES)):
                    raise ValueError('package ZIP has noncanonical, unsafe or oversized entries')
                names.add(entry_name)
                total += info.file_size
            if total > MAX_TOTAL_BYTES or '.package.json' not in names:
                raise ValueError('package ZIP content/manifest bound violated')
            raw = zipped.read('.package.json')  # Stored and bounded above; CRC is checked.
            if not raw.isascii() or b'\\' in raw:
                raise ValueError('ordinary manifest contains unsupported JSON escapes or non-ASCII bytes')
            manifest = json.loads(raw, object_pairs_hook=unique_object)
            validate_bundle_manifest(manifest, identity, version, architecture, kind)
            resources = source.get('payload') == 'resources'
            if manifest.get('resource_imports', []) != validate_resource_imports(source.get('resource_imports', [])):
                raise ValueError('bundled resource imports differ from source requests')
            if resources and manifest != resource_manifest(source_paths[0])[0]:
                raise ValueError('resource package differs from exact declared source bytes')
            if manifest['artifact'] != source.get('file_name') or manifest['requires'] != requirements:
                raise ValueError('bundled package artifact/dependencies differ from source manifest')
            if names != {'.package.json'} | {entry['name'] for entry in manifest['entries']}:
                raise ValueError('package ZIP has missing or undeclared files')
            with tempfile.TemporaryDirectory(prefix='rte-record-') as temporary:
                snapshot = Path(temporary)
                (snapshot / '.package.json').write_bytes(raw)
                for entry in manifest['entries']:
                    payload = zipped.read(entry['name'])
                    if len(payload) != entry['size_bytes'] or hashlib.sha256(payload).hexdigest() != entry['sha256']:
                        raise ValueError('package ZIP entry size/SHA-256 mismatch')
                    if kind != 'application' and not resources and entry['name'] == 'provider-abi.v1':
                        capability, api = source['provides'][0]['capability'], source['provides'][0]['api']
                        revision = source.get('os_cpu_abi', 1)
                        if type(revision) is not int or revision not in (1, 2):
                            raise ValueError('unsupported source OS/CPU ABI')
                        expected = f'os-cpu-abi={revision}\nprovides={capability}\napi={api}\n'.encode('ascii')
                        if payload != expected:
                            raise ValueError('bundled provider ABI differs from source manifest')
                    destination = snapshot / entry['name']
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(payload)
                if kind == 'application':
                    sidecar = bounded_json(snapshot, snapshot / f'{identity}.json', 2048)
                    expected_sidecar = dict(source)
                    executable = snapshot / manifest['artifact']
                    expected_sidecar['size_bytes'] = executable.stat().st_size
                    expected_sidecar['sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
                    if sidecar != expected_sidecar:
                        raise ValueError('bundled app sidecar differs from source/ELF integrity')
                expected_archive = pack_directory(snapshot)
            if archive != expected_archive:
                raise ValueError('exported package ZIP differs from canonical stored format')
    except (zipfile.BadZipFile, RuntimeError, KeyError) as exc:
        raise ValueError('package ZIP is corrupt or incomplete') from exc
    catalog = bounded_json(root, directory / 'package-catalog.json', 32768)
    if not isinstance(catalog, dict) or type(catalog.get('schema')) is not int or catalog['schema'] != 1:
        raise ValueError('generic package catalog schema is invalid')
    release = catalog.get('release')
    if not isinstance(release, str) or not re.fullmatch(r'[A-Za-z0-9._-]{1,63}', release) or '..' in release:
        raise ValueError('generic package catalog release is invalid')
    packages = catalog.get('packages')
    if not isinstance(packages, list) or not 1 <= len(packages) <= 64:
        raise ValueError('generic package catalog exceeds bounds')
    matches = [item for item in packages if isinstance(item, dict) and
               item.get('kind') == kind and item.get('id') == identity]
    expected_row = {'kind': kind, 'id': identity, 'version': version,
                    'artifact': manifest['artifact'], 'architecture': architecture,
                    'archive': name, 'size_bytes': len(archive),
                    'sha256': hashlib.sha256(archive).hexdigest()}
    if manifest.get('payload') == 'resources':
        expected_row['payload'] = 'resources'
    if len(matches) != 1 or matches[0] != expected_row:
        raise ValueError('generic catalog does not uniquely match the verified package ZIP')
    return manifest, archive_path, archive


def driver_bundle(identity: str, version: str, root: Path) -> tuple[dict, Path, bytes]:
    return package_bundle('driver', identity, version, root)


def build_record(product: str, identity: str, version: str, root: Path) -> dict:
    if product not in ("firmware", "apps", "drivers", "services", "providers"):
        raise ValueError("unsupported release product")
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        raise ValueError("version must be numeric MAJOR.MINOR.PATCH")
    prefix = {"firmware": "firmware", "apps": "app", "drivers": "driver", "services": "service", "providers": "provider"}[product]
    tag = f"{prefix}-v{version}" if product == "firmware" else f"{prefix}-{identity}-v{version}"
    if product == "firmware":
        asset_path = root / "dist/firmware-t5s3-pro.bin"
        manifest = None
    else:
        manifest, asset_path, archive = package_bundle(
            'application' if product == 'apps' else product[:-1], identity, version, root)
    size, sha256 = (len(archive), hashlib.sha256(archive).hexdigest()) if product != "firmware" else digest(asset_path)
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
    if product != "firmware":
        record["format"] = "rte.zip"
        record["architecture"] = manifest["architecture"]
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", required=True, choices=("firmware", "apps", "drivers", "services", "providers"))
    parser.add_argument("--id", default="")
    parser.add_argument("--version", required=True)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.product != "firmware" and not args.id:
        parser.error("--id is required for packages")
    record = build_record(args.product, args.id, args.version, args.root)
    args.output.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Verified {args.product} release record for {args.id or 'firmware'}")


if __name__ == "__main__":
    main()
