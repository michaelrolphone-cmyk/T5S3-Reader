#!/usr/bin/env python3
"""Build real signed archives and exercise bounded staging and extraction."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_risc_package as builder  # noqa: E402


def run() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        key = root / 'private.pem'
        public = root / 'public.pem'
        subprocess.run(['openssl', 'ecparam', '-name', 'prime256v1', '-genkey',
                        '-noout', '-out', str(key)], check=True, capture_output=True)
        subprocess.run(['openssl', 'pkey', '-in', str(key), '-pubout', '-out',
                        str(public)], check=True, capture_output=True)
        elf = root / 'driver.elf'
        data = bytearray(64)
        data[:6] = b'\x7fELF\x01\x01'
        data[16:20] = b'\x03\x00\x5e\x00'
        elf.write_bytes(data)
        resource = root / 'schema.json'
        resource.write_bytes(b'x' * 1200)  # Force multiple bounded copy chunks.
        args = argparse.Namespace(kind='driver', id='gps-nmea', version='1.2.3',
            artifact='driver.elf', architecture='xtensa-esp32s3',
            min_runtime_api='2', security_version='3', key_id='7',
            private_key=key, entry=[f'driver.elf={elf}', f'schema.json={resource}'],
            require=['kernel.serial:1'], output=root / 'candidate.risc')
        good = builder.build(args)
        alternate_path = root / 'alternate.risc'
        changed = builder.build(argparse.Namespace(**{
            **vars(args), 'security_version': '4', 'output': alternate_path}))
        assert good != changed and len(good) == len(changed) and len(good) > 1024
        # build() returns archive bytes; only its CLI writes the output file.
        args.output.write_bytes(good)
        alternate_path.write_bytes(changed)
        for name, test_source in [
            ('stage-test', 'package_archive_stage_test.cpp'),
            ('extract-test', 'package_archive_extract_test.cpp'),
        ]:
            program = root / name
            # Preserve compiler stderr in CI; sanitizer-backed compilation
            # failures must show the actual diagnostic, not only a traceback.
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            '-I' + str(ROOT / 'src'),
                            str(ROOT / 'test/resources' / test_source),
                            '-lcrypto', '-o', str(program)], check=True)
            subprocess.run([str(program), str(args.output), str(alternate_path),
                            str(public)], check=True)


if __name__ == '__main__':
    run()
