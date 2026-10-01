"""Declarative data-only service source, reused by existing package tooling."""
import hashlib
import json
from pathlib import Path
import re
if __package__:
    from .package_resource_paths import safe_resource_path, parent_paths, path_conflicts
else:
    from package_resource_paths import safe_resource_path, parent_paths, path_conflicts


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate resource source key')
        result[key] = value
    return result


def resource_source(path: Path):
    if path.is_symlink() or path.parent.is_symlink() or path.parent.parent.is_symlink() or path.parent.parent.name != 'Services' or not path.is_file() or path.stat().st_size > 4096:
        raise ValueError('unsafe or oversized resource source')
    with path.open('rb') as stream:
        raw = stream.read(4097)
    if len(raw) > 4096:
        raise ValueError('resource source grew beyond bound')
    source = json.loads(raw, object_pairs_hook=unique)
    required = {'type', 'id', 'version', 'payload', 'architecture', 'min_runtime_api', 'resources'}
    if not isinstance(source, dict) or set(source) != required or source['type'] != 'service' or source['payload'] != 'resources':
        raise ValueError('expected explicit resource-only service source')
    identity, version = source['id'], source['version']
    if (not isinstance(identity, str) or not 0 < len(identity) < 64 or
            not re.fullmatch(r'[a-z0-9](?:[a-z0-9_-]*[a-z0-9])?', identity)):
        raise ValueError('invalid resource service ID')
    if (not isinstance(version, str) or len(version) >= 32 or
            not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version) or
            any(int(part) > 0xffffffff for part in version.split('.'))):
        raise ValueError('invalid resource service version')
    if source['architecture'] not in ('xtensa-esp32s3', 'riscv32') or type(source['min_runtime_api']) is not int or not 0 < source['min_runtime_api'] <= 0xffffffff:
        raise ValueError('invalid resource target/runtime policy')
    names = source['resources']
    if not isinstance(names, list) or not 1 <= len(names) <= 16:
        raise ValueError('resource source requires one to sixteen files')
    seen = set()
    total = 0
    for name in names:
        if not safe_resource_path(name) or name.endswith('.elf') or path_conflicts(name, seen):
            raise ValueError('unsafe, executable or duplicate resource path')
        for parent in parent_paths(name):
            directory = path.parent / parent
            if directory.is_symlink() or not directory.is_dir():
                raise ValueError('resource parent is not a real source directory')
        resource = path.parent / name
        if resource.is_symlink() or not resource.is_file() or not 0 < resource.stat().st_size <= 1024*1024:
            raise ValueError('missing, unsafe or oversized resource')
        total += resource.stat().st_size
        if total > 4*1024*1024:
            raise ValueError('resource source exceeds total bound')
        seen.add(name)
    return source


def resource_manifest(path: Path):
    source = resource_source(path)
    entries = []
    payloads = {}
    for name in source['resources']:
        with (path.parent / name).open('rb') as stream:
            payload = stream.read(1024*1024+1)
        if not 0 < len(payload) <= 1024*1024:
            raise ValueError('resource changed beyond bound during read')
        payloads[name] = payload
        entries.append({'name': name, 'size_bytes': len(payload),
                        'sha256': hashlib.sha256(payload).hexdigest(), 'executable': False})
    manifest = {'schema': 3, 'kind': 'service', 'id': source['id'], 'version': source['version'],
                'payload': 'resources', 'artifact': None, 'architecture': source['architecture'],
                'min_runtime_api': source['min_runtime_api'], 'entries': entries,
                'requires': [], 'resource_imports': []}
    raw = (json.dumps(manifest, separators=(',', ':'), ensure_ascii=True)+'\n').encode('ascii')
    if len(raw) > 4096 or len(raw)+sum(len(data) for data in payloads.values()) > 4*1024*1024:
        raise ValueError('resource package exceeds manifest/content bound')
    return manifest, raw, payloads


def stage_resource_source(source: Path, target: Path):
    manifest, raw, payloads = resource_manifest(source)
    if target != source.parent.parent.parent / 'dist/packages' / manifest['id']:
        raise ValueError('resource staging target differs from canonical build identity')
    if any(parent.is_symlink() for parent in [target.parent, target.parent.parent]) or target.is_symlink() or (target.exists() and not target.is_dir()):
        raise ValueError('unsafe resource staging destination')
    # Common packer subsequently refuses any undeclared pre-existing files.
    target.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        current = target
        for component in name.split('/')[:-1]:
            current /= component
            if current.is_symlink() or (current.exists() and not current.is_dir()):
                raise ValueError('unsafe resource staging parent')
            current.mkdir(exist_ok=True)
        output = target / name
        if output.is_symlink():
            raise ValueError('unsafe resource staging leaf')
        output.write_bytes(payload)
    marker = target / '.package.json'
    if marker.is_symlink():
        raise ValueError('unsafe resource staging manifest')
    marker.write_bytes(raw)
    return manifest
