#!/usr/bin/env python3
"""Real P-256 cross-language provider admission / hostile signed profile tests."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_risc_package as builder  # noqa: E402


def openssl(*arguments: str) -> None:
    subprocess.run(['openssl', *arguments], check=True, capture_output=True)


def prefix_digest(archive: bytes) -> str:
    manifest_length = int.from_bytes(archive[12:16], 'little')
    return hashlib.sha256(archive[:48 + manifest_length]).hexdigest()


def run() -> None:
    with tempfile.TemporaryDirectory(prefix='risc-provider-profile-') as temporary:
        root = Path(temporary)
        key, public = root / 'private.pem', root / 'public.pem'
        openssl('ecparam', '-name', 'prime256v1', '-genkey', '-noout', '-out', str(key))
        openssl('pkey', '-in', str(key), '-pubout', '-out', str(public))
        elf = root / 'driver.elf'
        header = bytearray(64)
        header[:6] = b'\x7fELF\x01\x01'
        header[16:20] = b'\x03\x00\x5e\x00'
        elf.write_bytes(header)
        abi = root / 'provider-abi.v1'
        imports = root / 'privileged-imports.v1'
        output = root / 'fixture.risc'
        tester = root / 'profile-inspect'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-omit-frame-pointer', '-I' + str(ROOT / 'src'),
            str(ROOT / 'test/resources/package_provider_profile_inspect.cpp'),
            '-lcrypto', '-o', str(tester)], check=True, capture_output=True)
        good_profile = b'os-cpu-abi=1\nprovides=usb.controller\napi=1\n'
        good_imports = b'abort\nesp_intr_alloc\nmalloc\n'
        base_entries = [f'driver.elf={elf}', f'provider-abi.v1={abi}',
                        f'privileged-imports.v1={imports}']
        args = argparse.Namespace(kind='driver', id='usb-controller-test',
            version='1.2.3', artifact='driver.elf',
            architecture='xtensa-esp32s3', min_runtime_api='2',
            security_version='3', key_id='7', private_key=key,
            entry=base_entries, require=['i2c.bus:1'], output=output)
        cases = 0

        def check(label: str, *, profile: bytes = good_profile,
                  import_bytes: bytes = good_imports,
                  entries: list[str] | None = None, expected: bool = False,
                  tamper: str | None = None, version: str = '1.2.3',
                  pinned: str | None = None) -> str:
            nonlocal cases
            abi.write_bytes(profile)
            imports.write_bytes(import_bytes)
            options = argparse.Namespace(**{**vars(args), 'version': version,
                'entry': entries if entries is not None else base_entries})
            archive = bytearray(builder.build(options))
            fingerprint = prefix_digest(archive)
            if tamper == 'signature':
                signed_end = 48 + int.from_bytes(archive[12:16], 'little')
                archive[signed_end] ^= 1
            elif tamper == 'imports':
                payload = 48 + int.from_bytes(archive[12:16], 'little') + 64
                archive[payload + elf.stat().st_size] ^= 1
            output.write_bytes(archive)
            command = [str(tester), str(output), str(public), pinned or fingerprint,
                       'pass' if expected else 'reject']
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f'{label}: {result.stdout}\n{result.stderr}')
            cases += 1
            return fingerprint

        first = check('valid signed profile', expected=True)
        check('missing profile', entries=[base_entries[0], base_entries[2]])
        check('missing exact imports', entries=base_entries[:2])
        check('wrong signed ABI', profile=good_profile.replace(b'abi=1', b'abi=2'))
        check('wrong provided capability', profile=good_profile.replace(
            b'usb.controller', b'USB.controller'))
        check('zero API', profile=good_profile.replace(b'api=1', b'api=0'))
        check('noncanonical API', profile=good_profile.replace(b'api=1', b'api=01'))
        check('overflow API', profile=good_profile.replace(b'api=1', b'api=4294967296'))
        check('profile extra text', profile=good_profile + b'grant=usb\n')
        check('unsorted imports', import_bytes=b'malloc\nabort\nesp_intr_alloc\n')
        check('duplicate imports', import_bytes=b'abort\nabort\nesp_intr_alloc\n')
        check('unterminated imports', import_bytes=good_imports.rstrip(b'\n'))
        check('invalid import charset', import_bytes=b'abort\nesp.intr.alloc\nmalloc\n')
        check('empty import name', import_bytes=b'\n' + good_imports)
        check('too many import symbols', import_bytes=b''.join(
            f'n{i:03}\n'.encode('ascii') for i in range(129)))
        check('overlong import symbol', import_bytes=b'a' * 128 + b'\n')
        check('bad signature', tamper='signature')
        check('signed payload changed', tamper='imports')
        check('different valid signed package from pinned intake',
              expected=False, version='1.2.4', pinned=first)
        print(f'Signed provider profile: real P-256, immutable metadata, '
              f'{cases - 1} signed/tamper/rollback rejections PASS')


if __name__ == '__main__':
    run()
