#!/usr/bin/env python3
"""Exercise the real OpenSSL signing path and hostile package inputs."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_risc_package as builder  # noqa: E402


def der_integer(raw: bytes) -> bytes:
    value = raw.lstrip(b'\0') or b'\0'
    if value[0] & 128:
        value = b'\0' + value
    return b'\x02' + bytes([len(value)]) + value


def signature_to_der(raw: bytes) -> bytes:
    assert len(raw) == 64
    payload = der_integer(raw[:32]) + der_integer(raw[32:])
    return b'\x30' + bytes([len(payload)]) + payload


def openssl(*args: str, input: bytes | None = None) -> bytes:
    return subprocess.run(['openssl', *args], input=input, check=True, capture_output=True).stdout


def reject(args: argparse.Namespace, fragment: str) -> None:
    try:
        builder.build(args)
    except ValueError as error:
        assert fragment in str(error), str(error)
        return
    raise AssertionError(f'expected rejection containing {fragment}')


def run() -> None:
    assert builder.HEADER.size == 48 and builder.PREFIX.size == 16
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        key = root / 'key.pem'
        openssl('ecparam', '-name', 'prime256v1', '-genkey', '-noout', '-out', str(key))
        public = root / 'public.pem'
        openssl('pkey', '-in', str(key), '-pubout', '-out', str(public))
        elf = root / 'driver.elf'
        header = bytearray(64)
        header[:6] = b'\x7fELF\x01\x01'
        header[16:20] = b'\x03\x00\x5e\x00'
        elf.write_bytes(header)
        resource = root / 'schema.json'
        resource.write_bytes(b'{"name":"test"}')
        arguments = argparse.Namespace(kind='driver', id='gps-nmea', version='1.2.3',
            artifact='driver.elf', architecture='xtensa-esp32s3', min_runtime_api='2',
            security_version='3', key_id='7', private_key=key,
            entry=[f'driver.elf={elf}', f'schema.json={resource}'],
            require=['kernel.serial:1'], output=root / 'test.risc')
        archive = builder.build(arguments)
        magic, version, algorithm, manifest_size, entry_count, requirement_count, signature_size, reserved, payload_size, key_id, length, reserved2 = builder.HEADER.unpack_from(archive)
        assert (magic, version, algorithm, entry_count, requirement_count, signature_size, reserved, key_id, reserved2) == (b'RISCPKG1', 1, 1, 2, 1, 64, 0, 7, 0)
        assert length == len(archive) and payload_size == elf.stat().st_size + resource.stat().st_size
        signed_end = 48 + manifest_size
        signature = root / 'signature.der'
        signature.write_bytes(signature_to_der(archive[signed_end:signed_end + 64]))
        def verifies(data: bytes) -> bool:
            result = subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(public),
                '-signature', str(signature)], input=data, capture_output=True)
            return result.returncode == 0
        assert verifies(archive[:signed_end])
        tampered_manifest = bytearray(archive[:signed_end])
        tampered_manifest[-1] ^= 1
        assert not verifies(tampered_manifest)
        tampered_header = bytearray(archive[:signed_end])
        tampered_header[32] ^= 1
        assert not verifies(tampered_header)
        # Manifest records bind each exact entry's SHA-256 to the authenticated prefix.
        cursor = 48 + 16 + len(arguments.id) + len(arguments.version) + len(arguments.artifact) + len(arguments.architecture)
        digest = archive[cursor + 10:cursor + 42]
        assert digest == hashlib.sha256(elf.read_bytes()).digest()
        first_payload = signed_end + 64
        assert hashlib.sha256(archive[first_payload:first_payload + 64]).digest() == digest
        corrupt_payload = bytearray(archive)
        corrupt_payload[first_payload] ^= 1
        assert hashlib.sha256(corrupt_payload[first_payload:first_payload + 64]).digest() != digest
        assert verifies(corrupt_payload[:signed_end])  # Signature alone does not validate payloads.
        reject(argparse.Namespace(**{**vars(arguments), 'id': '../gps'}), 'invalid package ID')
        reject(argparse.Namespace(**{**vars(arguments), 'version': '01.2.3'}), 'version must be canonical')
        reject(argparse.Namespace(**{**vars(arguments), 'entry': [f'driver.elf={elf}'] * 2}), 'duplicate archive entry')
        reject(argparse.Namespace(**{**vars(arguments), 'entry': [f'../driver.elf={elf}']}), 'invalid entry name')
        reject(argparse.Namespace(**{**vars(arguments), 'entry': [f'schema.json={resource}']}), 'no entry')
        reject(argparse.Namespace(**{**vars(arguments), 'require': ['kernel.serial:1'] * 2}), 'duplicate capability requirement')
        resource.write_bytes(elf.read_bytes())
        reject(arguments, 'disguised as a resource')
        resource.write_bytes(b'{"name":"test"}')
        bad = root / 'wrong-curve.pem'
        openssl('ecparam', '-name', 'secp384r1', '-genkey', '-noout', '-out', str(bad))
        reject(argparse.Namespace(**{**vars(arguments), 'private_key': bad}), 'NIST P-256')
        result = subprocess.run([sys.executable, str(ROOT / 'scripts' / 'build_risc_package.py'),
            '--kind', 'driver', '--id', arguments.id, '--version', arguments.version,
            '--artifact', arguments.artifact, '--architecture', arguments.architecture,
            '--min-runtime-api', '2', '--security-version', '3', '--key-id', '7',
            '--private-key', str(key), '--entry', f'driver.elf={elf}',
            '--entry', f'schema.json={resource}', '--require', 'kernel.serial:1',
            '--output', str(arguments.output)], check=True, capture_output=True)
        assert arguments.output.is_file() and b'Built signed RISC-PKG v1' in result.stdout
    print('Package writer: P-256 signature verification, bound payloads and invalid-input rejection passed')


if __name__ == '__main__':
    run()
