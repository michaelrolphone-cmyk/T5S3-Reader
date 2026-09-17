#!/usr/bin/env python3
"""Emit canonical OS/CPU ABI v1 imports from a linked physical provider ELF.

This newline-delimited ASCII sidecar is UNSIGNED build output, not authority.
Only a trusted package-signing process covering the executable digest, ABI,
and sidecar digest together can subsequently authenticate this declaration.
The firmware C private matcher rechecks BOTH symbol tables independently.
"""
import argparse
import hashlib
from pathlib import Path
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

MAX_IMPORTS = 128
MAX_NAME = 127


def extract_imports(path: Path) -> list[str]:
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf.header['e_type'] != 'ET_DYN':
            raise ValueError('expected 32-bit little-endian native shared ELF')
        names: set[str] = set()
        dynsym = False
        for section in elf.iter_sections():
            if not isinstance(section, SymbolTableSection):
                continue
            if section['sh_type'] == 'SHT_DYNSYM':
                dynsym = True
            if section['sh_type'] not in ('SHT_DYNSYM', 'SHT_SYMTAB'):
                continue
            for index, symbol in enumerate(section.iter_symbols()):
                if symbol['st_shndx'] != 'SHN_UNDEF':
                    continue
                name = symbol.name
                if index == 0 and not name and symbol['st_info']['type'] == 'STT_NOTYPE':
                    continue
                if not name or len(name) > MAX_NAME or not name.isascii() or any(
                        ord(char) <= 0x20 or ord(char) > 0x7e for char in name):
                    raise ValueError('invalid undefined ELF symbol in ' + section.name)
                names.add(name)
        if not dynsym or not names or len(names) > MAX_IMPORTS:
            raise ValueError('missing .dynsym or invalid exact import count')
        return sorted(names)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path, default=None)
    args = parser.parse_args()
    try:
        imports = extract_imports(args.elf)
        declaration = (''.join(name + '\n' for name in imports)).encode('ascii')
        output = args.output or args.elf.with_name('privileged-imports.v1')
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(declaration)
        print(f'Generated ABI v1 import declaration: {output} ({len(imports)} names)')
        print(f'Executable SHA-256: {hashlib.sha256(args.elf.read_bytes()).hexdigest()}')
        print(f'Import sidecar SHA-256: {hashlib.sha256(declaration).hexdigest()}')
        print('UNSIGNED diagnostic: package manager must authenticate BOTH file digests and ABI.')
        return 0
    except (OSError, ValueError, UnicodeEncodeError) as error:
        parser.exit(1, f'Privileged imports build rejected: {error}\n')


if __name__ == '__main__':
    sys.exit(main())
