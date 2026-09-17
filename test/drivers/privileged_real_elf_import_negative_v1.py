#!/usr/bin/env python3
"""Negative exact-import tests against real, freshly linked provider ELF bytes.

The compiled firmware C matcher must reject every changed canonical declaration
(including an EXTRA ABI-PERMITTED import) even when the original ELF and
canonical unsigned build sidecar agree. These are not signature tests.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ALLOWED = ('malloc', 'printf', 'memset', 'strcpy', 'strlen', 'esp_intr_alloc',
           'xTaskGetTickCount', 'calloc', 'free', 'memcpy', 'strchr', 'vfprintf')


def invoke(matcher: Path, elf: Path, declaration: Path, expected: bool) -> None:
    result = subprocess.run([str(matcher), str(elf), str(declaration)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, check=False)
    if (result.returncode == 0) != expected:
        raise AssertionError(f'{elf}: expected {"accepted" if expected else "rejected"} '
                             f'but exit={result.returncode}\n{result.stdout}\n{result.stderr}')


def exercise(matcher: Path, elf: Path) -> None:
    sidecar = elf.with_name('privileged-imports.v1')
    original = sidecar.read_bytes()
    if not original or not original.endswith(b'\n'):
        raise AssertionError(f'{elf}: missing canonical generated import sidecar')
    names = [line.decode('ascii') for line in original.splitlines()]
    if not names or names != sorted(set(names)):
        raise AssertionError(f'{elf}: noncanonical generated sidecar')
    invoke(matcher, elf, sidecar, True)
    extra = next((name for name in ALLOWED if name not in names), None)
    if not extra:
        raise AssertionError('No absent ABI-permitted symbol available for extra-import test')
    with tempfile.TemporaryDirectory(prefix='riscrte-import-negative-') as temp:
        trial = Path(temp) / 'privileged-imports.v1'
        def reject(candidate: bytes, label: str) -> None:
            trial.write_bytes(candidate)
            invoke(matcher, elf, trial, False)
            print(f'  Rejected {label}: {elf.name}', flush=True)
        reject((''.join(name + '\n' for name in names[1:])).encode('ascii'),
               'missing actual ELF import')
        reject((''.join(name + '\n' for name in sorted([*names, extra]))).encode('ascii'),
               'extra ABI-permitted but undeclared ELF import')
        reject(original + names[-1].encode('ascii') + b'\n', 'duplicate import')
        if len(names) >= 2:
            swapped = names.copy()
            swapped[0], swapped[1] = swapped[1], swapped[0]
            reject((''.join(name + '\n' for name in swapped)).encode('ascii'),
                   'out-of-order import')
        reject(original.replace(b'\n', b'\r\n', 1), 'CRLF / noncanonical encoding')
    print(f'Real ELF import set negative checks PASS: {elf}', flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('matcher', type=Path)
    parser.add_argument('elf', nargs='+', type=Path)
    args = parser.parse_args()
    for elf in args.elf:
        exercise(args.matcher.absolute(), elf.absolute())


if __name__ == '__main__':
    main()
