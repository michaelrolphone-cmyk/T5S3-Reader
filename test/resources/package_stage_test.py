#!/usr/bin/env python3
"""Build real signed archives and exercise bounded copy/reverification staging."""
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
        changed = builder.build(argparse.Namespace(**{
            **vars(args), 'security_version': '4', 'output': root / 'alternate.risc'}))
        assert good != changed and len(good) == len(changed) and len(good) > 1024
        program = root / 'stage-test'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(ROOT / 'src'),
                        str(ROOT / 'test/resources/package_archive_stage_test.cpp'),
                        '-lcrypto', '-o', str(program)], check=True, capture_output=True)
        subprocess.run([str(program), str(args.output), str(root / 'alternate.risc'),
                        str(public)], check=True)


if __name__ == '__main__':
    run()
