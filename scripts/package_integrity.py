"""Bind release ELF size/SHA-256 to a manifest (integrity, not authenticity)."""
import hashlib
import json
from pathlib import Path

MAX_APP_MANIFEST_BYTES = 2048


def stamp_app_manifest(manifest: dict, elf: Path) -> dict:
    """Return metadata for a built ELF, refusing contradictory existing digests."""
    if not isinstance(manifest, dict) or not elf.is_file() or elf.suffix != '.elf':
        raise ValueError('invalid app manifest or artifact')
    if manifest.get('file_name') != elf.name:
        raise ValueError('ELF filename and manifest disagree')
    length = elf.stat().st_size
    if length < 52 or length > 1024 * 1024:
        raise ValueError('ELF size outside app package budget')
    digest = hashlib.sha256()
    with elf.open('rb') as stream:
        for chunk in iter(lambda: stream.read(16 * 1024), b''):
            digest.update(chunk)
    size = manifest.get('size_bytes', length)
    sha = manifest.get('sha256', digest.hexdigest())
    if type(size) is not int or size != length or sha != digest.hexdigest():
        raise ValueError('existing digest or size does not match actual ELF')
    stamped = {**manifest, 'size_bytes': length, 'sha256': digest.hexdigest()}
    raw = json.dumps(stamped, separators=(',', ':'), ensure_ascii=False).encode('utf-8') + b'\n'
    if len(raw) > MAX_APP_MANIFEST_BYTES:
        raise ValueError('release metadata exceeds runtime manifest limit')
    return stamped
