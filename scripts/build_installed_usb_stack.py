#!/usr/bin/env python3
"""Build and bundle every ABI-v2 provider discovered from source manifests.

A source manifest plus its ordinary build_<source-directory>.py script is the
extension point for a new class. Existing linked outputs are reused, including
the board-specific controller/I2C probes. No USB class ID, count or version is
baked into the distribution path. No flashing or publication occurs here.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from generate_provider_package_inputs_v1 import canonical_manifest, prepare as provider_inputs
from generate_privileged_imports_v1 import extract_imports
from pack_rte_zip import catalog_row, pack_directory
from verify_provider_relocation_map import audit_loader_map

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


def discovered() -> list[dict]:
    """Select real ABI-v2 manifests and reject missing/ambiguous build outputs."""
    candidates = []
    identities = set()
    for path in sorted(DRIVER_SOURCES.glob('*/manifest.json')):
        metadata = json.loads(path.read_text(encoding='utf-8'))
        if metadata.get('type') != 'driver' or metadata.get('driver_abi') != 2:
            continue
        capability, api = canonical_manifest(path)
        identity = metadata['id']
        version = metadata.get('version')
        if not isinstance(version, str) or not VERSION.fullmatch(version):
            raise ValueError(f'{identity}: invalid numeric package version {version!r}')
        if identity in identities:
            raise ValueError(f'duplicate ABI-v2 package identity: {identity}')
        identities.add(identity)
        requirements = metadata['requires']
        candidates.append({'id': identity, 'source': path, 'metadata': metadata,
                           'elf': linked_or_build(identity, path.parent),
                           'capability': capability, 'api': api, 'version': version,
                           'requires': [required['capability'] for required in requirements]})
        if len(candidates) > MAX_PACKAGES:
            raise ValueError('provider count exceeds firmware package catalog bound')
    if not candidates:
        raise ValueError('no linked ABI-v2 provider manifests found')
    return candidates


def dependency_order(candidates: list[dict]) -> list[dict]:
    """Topologically stage dependencies; multiple class ELFs may offer serial.port."""
    offered = {candidate['capability'] for candidate in candidates}
    for candidate in candidates:
        missing = set(candidate['requires']) - offered
        if missing:
            raise ValueError(f"{candidate['id']}: missing linked dependencies {sorted(missing)}")
    ordered = []
    available = set()
    pending = list(candidates)
    while pending:
        ready = next((candidate for candidate in pending
                      if set(candidate['requires']) <= available), None)
        if ready is None:
            raise ValueError('cyclic/unresolvable provider manifest dependency graph: ' +
                             ', '.join(item['id'] for item in pending))
        pending.remove(ready)
        ordered.append(ready)
        available.add(ready['capability'])
    return ordered


def build() -> list[dict]:
    candidates = dependency_order(discovered())
    DESTINATION.mkdir(parents=True, exist_ok=True)
    catalog = []
    for candidate in candidates:
        identity = candidate['id']
        metadata = candidate['metadata']
        elf = candidate['elf']
        mapping = audit_loader_map(elf)
        if (mapping['unmapped_relocations'] or mapping['unmapped_relative_values'] or
                mapping['unmapped_executable_sections']):
            raise ValueError(f'provider {identity} is not relocatable by runtime: '
                             f"{len(mapping['unmapped_relocations'])} unmapped sites; "
                             f"{len(mapping['unmapped_relative_values'])} invalid values; "
                             f"executable orphans={mapping['unmapped_executable_sections']}")
        imports = extract_imports(elf)
        # The sole permitted U1 raw I2C importer is the installed bus ELF.
        is_bus = candidate['capability'] == 'i2c.bus'
        if ((BRIDGE in imports) != is_bus or
                (is_bus and mapping['absolute_peripheral_relocations'])):
            raise ValueError(f'firmware I2C bridge isolation violated by {identity}')
        target = DESTINATION / identity
        target.mkdir(parents=True, exist_ok=True)
        executable = target / 'driver.elf'
        shutil.copyfile(elf, executable)
        provider_inputs(executable, candidate['source'], target)
        dependencies = [{'capability': required['capability'], 'min_api': required['api']}
                        for required in metadata['requires']]
        entries = [entry(target / name, name == 'driver.elf') for name in
                   ('driver.elf', 'provider-abi.v1', 'privileged-imports.v1')]
        package = {'schema': 1, 'kind': 'driver', 'id': identity,
                   'version': candidate['version'], 'artifact': 'driver.elf',
                   'architecture': metadata['architecture'], 'min_runtime_api': 2,
                   'entries': entries, 'requires': dependencies}
        encoded = (json.dumps(package, separators=(',', ':'), ensure_ascii=True) + '\n').encode('ascii')
        if len(encoded) > 4096:
            raise ValueError(f'manifest exceeds device parser bound: {identity}')
        (target / '.package.json').write_bytes(encoded)
        asset_name = f"driver-{identity}-{candidate['version']}-{metadata['architecture']}.rte.zip"
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
    try:
        build()
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f'Provider package build FAILED: {exc}', file=sys.stderr)
        sys.exit(1)
