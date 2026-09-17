#!/usr/bin/env python3
"""Build a signed, uncompressed RISC-PKG v1 archive (never stores private keys)."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

HEADER = struct.Struct('<8sHHIHHHHQIQI')  # 48 bytes; docs/RISC_PACKAGE_FORMAT.md
PREFIX = struct.Struct('<BBBBBBHII')       # 16-byte binary manifest prefix
MAX_ENTRIES = 16
MAX_REQUIREMENTS = 16
MAX_ENTRY = 1024 * 1024
MAX_TOTAL = 4 * 1024 * 1024
P256_OID = bytes.fromhex('06082a8648ce3d030107')
KINDS = {'application': 0, 'driver': 1, 'service': 2, 'provider': 3}
ELF_MACHINES = {b'xtensa-esp32s3': 94, b'riscv32': 243}
SAFE_ID = re.compile(r'[a-z0-9](?:[a-z0-9_-]*[a-z0-9])?\Z', re.ASCII)
SAFE_NAME = re.compile(r'[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?\Z', re.ASCII)
SAFE_CAPABILITY = re.compile(r'[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?\Z', re.ASCII)
VERSION = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z', re.ASCII)


def identifier(value: str, maximum: int, pattern: re.Pattern[str], label: str) -> bytes:
    if not pattern.fullmatch(value) or '..' in value:
        raise ValueError(f'invalid {label}: {value!r}')
    encoded = value.encode('ascii')
    if not 0 < len(encoded) < maximum:
        raise ValueError(f'{label} length exceeds its bounded field')
    return encoded


def parse_version(value: str) -> bytes:
    matched = VERSION.fullmatch(value)
    if not matched or any(int(n) > 0xFFFFFFFF for n in matched.groups()):
        raise ValueError('version must be canonical uint32 major.minor.patch')
    return identifier(value, 32, SAFE_NAME, 'version')


def decimal(value: str, label: str, maximum: int = 0xFFFFFFFF) -> int:
    if not value.isascii() or not value.isdecimal():
        raise ValueError(f'{label} must be a decimal integer')
    number = int(value)
    if not 0 < number <= maximum:
        raise ValueError(f'{label} must be between 1 and {maximum}')
    return number


def parse_assignment(text: str, label: str, separator: str = '=') -> tuple[str, str]:
    if separator not in text:
        raise ValueError(f'{label} must use NAME{separator}VALUE')
    key, value = text.split(separator, 1)
    if not key or not value:
        raise ValueError(f'{label} requires a nonempty name and value')
    return key, value


def der_length(data: bytes, position: int) -> tuple[int, int]:
    if position >= len(data):
        raise ValueError('truncated ASN.1 length')
    first = data[position]
    position += 1
    if first < 128:
        return first, position
    count = first & 127
    if not count or count > 2 or position + count > len(data):
        raise ValueError('unsupported DER length')
    value = int.from_bytes(data[position:position + count], 'big')
    if value < 128 or (count > 1 and data[position] == 0):
        raise ValueError('nonminimal DER length')
    return value, position + count


def der_ecdsa_to_raw(der: bytes) -> bytes:
    if not der or der[0] != 0x30:
        raise ValueError('ECDSA signature is not a DER sequence')
    length, position = der_length(der, 1)
    if position + length != len(der):
        raise ValueError('DER signature length mismatch')
    values: list[bytes] = []
    for _ in range(2):
        if position >= len(der) or der[position] != 0x02:
            raise ValueError('expected DER signature integer')
        length, position = der_length(der, position + 1)
        number = der[position:position + length]
        position += length
        if not number or number[0] & 128 or (len(number) > 1 and number[0] == 0 and number[1] < 128):
            raise ValueError('invalid/nonminimal DER integer')
        integer = int.from_bytes(number, 'big')
        if not 0 < integer < 1 << 256:
            raise ValueError('ECDSA integer does not fit P-256')
        values.append(integer.to_bytes(32, 'big'))
    if position != len(der):
        raise ValueError('trailing DER signature bytes')
    return b''.join(values)


def sign(prefix: bytes, key: Path) -> bytes:
    public = subprocess.run(['openssl', 'pkey', '-in', str(key), '-pubout', '-outform', 'DER'],
                            capture_output=True, check=True).stdout
    if P256_OID not in public:
        raise ValueError('private signing key must be on NIST P-256 (prime256v1)')
    signature = subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(key)],
                               input=prefix, capture_output=True, check=True).stdout
    result = der_ecdsa_to_raw(signature)
    if len(result) != 64:
        raise ValueError('unexpected signature length')
    return result


def build(arguments: argparse.Namespace) -> bytes:
    kind = KINDS[arguments.kind]
    package_id = identifier(arguments.id, 64, SAFE_ID, 'package ID')
    version = parse_version(arguments.version)
    artifact = identifier(arguments.artifact, 128, SAFE_NAME, 'artifact')
    if not artifact.endswith(b'.elf'):
        raise ValueError('executable artifact must end in .elf')
    arch = identifier(arguments.architecture, 32, SAFE_CAPABILITY, 'architecture')
    if arch not in ELF_MACHINES:
        raise ValueError('unsupported architecture for ELF header verification')
    min_api = decimal(arguments.min_runtime_api, 'minimum runtime API')
    security = decimal(arguments.security_version, 'security version')
    key_id = decimal(arguments.key_id, 'signing key ID')

    if not 1 <= len(arguments.entry) <= MAX_ENTRIES:
        raise ValueError('package must have 1–16 entries')
    contents: dict[bytes, bytes] = {}
    for definition in arguments.entry:
        name, source = parse_assignment(definition, '--entry')
        basename = identifier(name, 128, SAFE_NAME, 'entry name')
        if basename in contents:
            raise ValueError('duplicate archive entry')
        if basename.endswith(b'.elf') and basename != artifact:
            raise ValueError('undeclared ELF entry')
        with open(source, 'rb') as handle:
            data = handle.read(MAX_ENTRY + 1)
            if not 0 < len(data) <= MAX_ENTRY or handle.read(1):
                raise ValueError(f'entry {name} exceeds bounded size or is empty')
        if basename == artifact:
            if (len(data) < 52 or data[:6] != b'\x7fELF\x01\x01' or
                    data[16:18] != b'\x03\x00' or
                    int.from_bytes(data[18:20], 'little') != ELF_MACHINES[arch]):
                raise ValueError('executable ELF header does not match architecture')
        elif data.startswith(b'\x7fELF'):
            raise ValueError('ELF payload disguised as a resource')
        contents[basename] = data
    if artifact not in contents:
        raise ValueError('declared executable has no entry')
    total = sum(len(data) for data in contents.values())
    if total > MAX_TOTAL:
        raise ValueError('aggregate entry bytes exceed package limit')
    if len(arguments.require) > MAX_REQUIREMENTS:
        raise ValueError('too many requirements')
    requirements: dict[bytes, int] = {}
    for definition in arguments.require:
        name, api = parse_assignment(definition, '--require', ':')
        name_bytes = identifier(name, 64, SAFE_CAPABILITY, 'capability requirement')
        if name_bytes in requirements:
            raise ValueError('duplicate capability requirement')
        requirements[name_bytes] = decimal(api, 'required API')

    manifest = bytearray(PREFIX.pack(kind, len(package_id), len(version), len(artifact), len(arch),
                                     0, 0, min_api, security))
    manifest += package_id + version + artifact + arch
    for name, data in sorted(contents.items()):
        manifest += struct.pack('<BBQ', len(name), int(name == artifact), len(data))
        manifest += hashlib.sha256(data).digest() + name
    for name, api in sorted(requirements.items()):
        manifest += struct.pack('<BI', len(name), api) + name
    if not 16 <= len(manifest) <= 4096:
        raise ValueError('manifest exceeds its 4096-byte bound')
    complete = HEADER.size + len(manifest) + 64 + total
    header = HEADER.pack(b'RISCPKG1', 1, 1, len(manifest), len(contents), len(requirements),
                         64, 0, total, key_id, complete, 0)
    signed = header + manifest
    signature = sign(signed, arguments.private_key)
    result = signed + signature + b''.join(contents[name] for name in sorted(contents))
    if len(result) != complete:
        raise ValueError('internal archive length mismatch')
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=sorted(KINDS), required=True)
    parser.add_argument('--id', required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--artifact', required=True)
    parser.add_argument('--architecture', required=True)
    parser.add_argument('--min-runtime-api', required=True)
    parser.add_argument('--security-version', required=True)
    parser.add_argument('--key-id', required=True)
    parser.add_argument('--private-key', required=True, type=Path)
    parser.add_argument('--entry', action='append', default=[], metavar='NAME=FILE')
    parser.add_argument('--require', action='append', default=[], metavar='CAPABILITY:API')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        archive = build(args)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # Do not write output until every bound and the signing operation succeeds.
        with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix='.risc-', delete=False) as temp:
            temporary = Path(temp.name)
            temp.write(archive)
            temp.flush()
            os.fsync(temp.fileno())
        try:
            os.replace(temporary, args.output)
        finally:
            temporary.unlink(missing_ok=True)
        print(f'Built signed RISC-PKG v1: {args.output} ({len(archive)} bytes)')
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'Package build rejected: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
