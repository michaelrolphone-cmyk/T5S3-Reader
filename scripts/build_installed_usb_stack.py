#!/usr/bin/env python3
"""Build and bundle every ABI-v2 provider discovered from source manifests.

A source manifest plus its ordinary build_<source-directory>.py script is the
extension point for a new class. Existing linked outputs are reused, including
the board-specific controller/I2C probes. No USB class ID, count or version is
baked into the distribution path. No flashing or publication occurs here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

if __package__:
    from .generate_provider_package_inputs_v1 import canonical_manifest, prepare as provider_inputs
    from .generate_privileged_imports_v1 import extract_imports
    from .pack_rte_zip import catalog_row, pack_directory
    from .verify_provider_relocation_map import audit_loader_map
    from .package_resource_source import resource_source, stage_resource_source
    from .package_resource_paths import validate_resource_imports
else:
    from generate_provider_package_inputs_v1 import canonical_manifest, prepare as provider_inputs
    from generate_privileged_imports_v1 import extract_imports
    from pack_rte_zip import catalog_row, pack_directory
    from verify_provider_relocation_map import audit_loader_map
    from package_resource_source import resource_source, stage_resource_source
    from package_resource_paths import validate_resource_imports

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'dist/experimental'
DESTINATION = ROOT / 'dist/packages'
DRIVER_SOURCES = ROOT / 'Drivers'
BRIDGE = 'risc_fw_i2c_transact_v1'
VERSION = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')
MAX_PACKAGES = 64


def entry(path: Path, executable: bool) -> dict:
    payload = path.read_bytes()
    if not payload:
        raise ValueError(f'empty package entry: {path}')
    return {'name': path.name, 'size_bytes': len(payload),
            'sha256': hashlib.sha256(payload).hexdigest(),
            'executable': executable}


def linked_elf(identity: str) -> Path:
    """Resolve one linked ELF by manifest ID, permitting an old build prefix."""
    if not SOURCE.is_dir():
        raise FileNotFoundError(f'linked ELF output root missing: {SOURCE}')
    folders = sorted(path for path in SOURCE.iterdir()
                     if path.is_dir() and not path.is_symlink() and
                     (path.name == identity or path.name.endswith('-' + identity)))
    if not folders:
        raise FileNotFoundError(f'{identity}: linked ELF output directory missing')
    if len(folders) != 1:
        raise ValueError(f'{identity}: ambiguous linked ELF directories: {folders}')
    preferred = folders[0] / 'driver.elf'
    elfs = sorted(path for path in folders[0].glob('*.elf')
                  if path.is_file() and not path.is_symlink())
    if not elfs:
        raise FileNotFoundError(f'{identity}: linked ELF missing in {folders[0]}')
    if len(elfs) != 1:
        raise ValueError(f'{identity}: ambiguous linked ELFs: {elfs}')
    elf = preferred if preferred in elfs else elfs[0]
    if elf.stat().st_size < 52:
        raise ValueError(f'{identity}: empty/truncated linked ELF: {elf}')
    return elf


def linked_or_build(identity: str, source_directory: Path) -> Path:
    """A new class joins with a manifest and build_<source-directory>.py.

    Only genuinely missing linked output permits a build. Ambiguous, stale or
    truncated files never trigger a build that could hide an unsafe artifact.
    The script is a repository-owned deterministic path, not manifest-supplied
    executable text or a filename from a downloaded catalog.
    """
    try:
        return linked_elf(identity)
    except FileNotFoundError:
        name = source_directory.name
        if not re.fullmatch(r'[a-z0-9_]+', name) or source_directory.is_symlink():
            raise ValueError(f'unsafe provider build source: {source_directory}')
        script = ROOT / 'scripts' / f'build_{name}.py'
        if not script.is_file() or script.is_symlink():
            raise FileNotFoundError(
                f'{identity}: missing ELF and no conventional builder {script.name}')
        print(f'Building newly discovered provider {identity} using {script.name}', flush=True)
        subprocess.run([sys.executable, str(script)], cwd=ROOT, check=True)
        return linked_elf(identity)


def source_candidates(root: Path | None = None, allow_empty: bool = False) -> list[dict]:
    """Validate the source graph before any artifact access or build side effects."""
    candidates = []
    identities = set()
    source_root = root / 'Drivers' if root is not None else DRIVER_SOURCES
    roots = (source_root, source_root.parent / 'Services', source_root.parent / 'Providers')
    for path in sorted(path for root in roots for path in root.glob('*/manifest.json')):
        if path.is_symlink() or path.parent.is_symlink() or path.stat().st_size > 4096:
            raise ValueError(f'unsafe or oversized provider manifest: {path}')
        metadata = json.loads(path.read_text(encoding='utf-8'))
        if not isinstance(metadata, dict):
            raise ValueError(f'invalid provider manifest: {path}')
        resources = metadata.get('payload') == 'resources'
        if resources:
            metadata = resource_source(path)
            metadata['requires'] = []
        elif metadata.get('type') not in ('driver', 'service', 'provider') or metadata.get('driver_abi') != 2:
            continue
        # Enforce the bound before invoking any newly discovered build script.
        if len(candidates) >= MAX_PACKAGES:
            raise ValueError('provider count exceeds firmware package catalog bound')
        capability, api = (None, 0) if resources else canonical_manifest(path)
        identity = metadata['id']
        version = metadata.get('version')
        if not isinstance(version, str) or not VERSION.fullmatch(version):
            raise ValueError(f'{identity}: invalid numeric package version {version!r}')
        if identity in identities:
            raise ValueError(f'duplicate ABI-v2 package identity: {identity}')
        identities.add(identity)
        requirements = metadata['requires']
        candidates.append({'id': identity, 'source': path, 'metadata': metadata,
                           'capability': capability, 'api': api, 'version': version, 'resources_only': resources,
                           'requires': [required['capability'] for required in requirements]})
    if not candidates and not allow_empty:
        raise ValueError('no ABI-v2 provider source manifests found')
    return candidates


def dependency_order(candidates: list[dict]) -> list[dict]:
    """Topologically stage dependencies with minimum API versions.

    Multiple independently installable class providers may expose the same
    capability. Only a provider with a sufficient declared API can satisfy a
    dependency; a matching string with a lower version is not sufficient.
    """
    offered = {}
    for candidate in candidates:
        name = candidate['capability']
        if name:
            offered[name] = max(offered.get(name, 0), candidate['api'])
    for candidate in candidates:
        missing = [f"{required['capability']}@{required['api']}"
                   for required in candidate['metadata']['requires']
                   if offered.get(required['capability'], 0) < required['api']]
        if missing:
            raise ValueError(f"{candidate['id']}: missing compatible linked dependencies {missing}")
    ordered = []
    available = {}
    pending = list(candidates)
    while pending:
        ready = next((candidate for candidate in pending
                      if all(available.get(required['capability'], 0) >= required['api']
                             for required in candidate['metadata']['requires'])), None)
        if ready is None:
            raise ValueError('cyclic/unresolvable provider manifest dependency graph: ' +
                             ', '.join(item['id'] for item in pending))
        pending.remove(ready)
        ordered.append(ready)
        name = ready['capability']
        if name:
            available[name] = max(available.get(name, 0), ready['api'])
    return ordered


def discovered(identities: set[str] | None = None) -> list[dict]:
    """Link only selected packages; all dependency metadata remains validated.

    Dependencies may already be installed from independent package releases.
    Selecting one package must not rebuild or republish those dependencies.
    The default still links and bundles the complete source graph.
    """
    candidates = dependency_order(source_candidates())
    valid_ids = {candidate['id'] for candidate in candidates}
    if identities is not None and (not identities or not identities <= valid_ids):
        raise ValueError(f"invalid requested driver IDs: {sorted(identities - valid_ids)}")
    selected = [candidate for candidate in candidates
                if identities is None or candidate['id'] in identities]
    for candidate in selected:
        if not candidate['resources_only']:
            candidate['elf'] = linked_or_build(candidate['id'], candidate['source'].parent)
    return selected


def build(identities: set[str] | None = None) -> list[dict]:
    candidates = discovered(identities)
    DESTINATION.mkdir(parents=True, exist_ok=True)
    catalog = []
    for candidate in candidates:
        identity = candidate['id']
        metadata = candidate['metadata']
        if candidate['resources_only']:
            target = DESTINATION / identity
            package = stage_resource_source(candidate['source'], target)
            asset_name = f"service-{identity}-{candidate['version']}-{metadata['architecture']}.rte.zip"
            archive = pack_directory(target)
            (DESTINATION / asset_name).write_bytes(archive)
            catalog.append(catalog_row(target, asset_name, archive))
            print(f'Resource-only service: {identity}; no ELF, capability or activation')
            continue
        elf = candidate['elf']
        mapping = audit_loader_map(elf)
        if (mapping['unmapped_relocations'] or mapping['unmapped_relative_values'] or
                mapping['unmapped_executable_sections']):
            raise ValueError(f'provider {identity} is not relocatable by runtime: '
                             f"{len(mapping['unmapped_relocations'])} unmapped sites; "
                             f"{len(mapping['unmapped_relative_values'])} invalid values; "
                             f"executable orphans={mapping['unmapped_executable_sections']}")
        imports = extract_imports(elf)
        # Only the transitional ESP32-S3 adapter may import the firmware I2C
        # bridge. X4's separate i2c.bus ELF owns its GPIO bus directly.
        is_firmware_i2c_adapter = identity == 'i2c-esp32s3-v2' and candidate['capability'] == 'i2c.bus'
        if ((BRIDGE in imports) != is_firmware_i2c_adapter or
                (is_firmware_i2c_adapter and mapping['absolute_peripheral_relocations'])):
            raise ValueError(f'firmware I2C bridge isolation violated by {identity}')
        target = DESTINATION / identity
        target.mkdir(parents=True, exist_ok=True)
        executable = target / 'driver.elf'
        shutil.copyfile(elf, executable)
        provider_inputs(executable, candidate['source'], target)
        dependencies = [{'capability': required['capability'], 'min_api': required['api']}
                        for required in metadata['requires']]
        names = ['driver.elf', 'provider-abi.v1', 'privileged-imports.v1']
        # Revision >1 must carry its digest-bound source selection. Both the
        # installed resolver and graph deliberately reject a newer profile
        # without this manifest; a profile alone cannot upgrade legacy ABI1.
        if metadata.get('os_cpu_abi', 1) > 1:
            shutil.copyfile(candidate['source'], target / 'manifest.json')
            names.append('manifest.json')
        entries = [entry(target / name, name == 'driver.elf') for name in names]
        package = {'schema': 1, 'kind': metadata['type'], 'id': identity,
                   'version': candidate['version'], 'artifact': 'driver.elf',
                   'architecture': metadata['architecture'], 'min_runtime_api': 2,
                   'entries': entries, 'requires': dependencies}
        resource_imports = validate_resource_imports(metadata.get('resource_imports', []))
        if resource_imports:
            package.update(schema=3, payload='executable', resource_imports=resource_imports)
        encoded = (json.dumps(package, separators=(',', ':'), ensure_ascii=True) + '\n').encode('ascii')
        if len(encoded) > 4096:
            raise ValueError(f'manifest exceeds device parser bound: {identity}')
        (target / '.package.json').write_bytes(encoded)
        asset_name = f"{metadata['type']}-{identity}-{candidate['version']}-{metadata['architecture']}.rte.zip"
        archive = pack_directory(target)
        (DESTINATION / asset_name).write_bytes(archive)
        catalog.append(catalog_row(target, asset_name, archive))
        print(f"Installable: {identity}@{candidate['version']} -> {asset_name} "
              f"({candidate['capability']}@{candidate['api']})", flush=True)
    release = os.environ.get('RISC_PACKAGE_RELEASE', 'unpublished-build')
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,63}', release):
        raise ValueError('invalid immutable release identifier')
    (DESTINATION / 'package-catalog.json').write_text(
        json.dumps({'schema': 1, 'release': release, 'packages': catalog},
                   indent=2) + '\n', encoding='utf-8')
    print(f'{len(catalog)} manifest-discovered providers bundled; '
          'no ELF activated or firmware flashed.', flush=True)
    return catalog


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ids', nargs='+', help='build only these canonical module package IDs')
    args = parser.parse_args()
    try:
        build(set(args.ids) if args.ids else None)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f'Provider package build FAILED: {exc}', file=sys.stderr)
        sys.exit(1)
