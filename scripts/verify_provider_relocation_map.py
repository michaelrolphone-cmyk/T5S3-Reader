#!/usr/bin/env python3
"""Check Xtensa provider relocation destinations against the *runtime* map.

esp_elf_load_section maps .text, .data, .rodata, .data.rel.ro and .bss;
esp_elf_map_sym has no generic ELF-section fallback. A RELA destination in
an otherwise valid ALLOC orphan section fails during relocation (-EINVAL).
An executable orphan can also fail later when entered without an ELF relocation.
"""
import argparse
from pathlib import Path
import struct

MAPPED = frozenset(('.text', '.data', '.rodata', '.data.rel.ro', '.bss'))
SHF_ALLOC = 2
SHF_EXECINSTR = 4
SHT_RELA = 4
SHT_NOBITS = 8


def audit_loader_map(path):
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('not a supported little-endian ELF32')
    _, _, _, _, _, shoff, _, ehsize, _, _, shentsize, shnum, name_index = (
        struct.unpack_from('<HHIIIIIHHHHHH', data, 16))
    if ehsize != 52 or shentsize != 40 or not 0 < shnum <= 256 or name_index >= shnum or shoff > len(data) or shnum * shentsize > len(data) - shoff:
        raise ValueError('invalid ELF section header table')
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i*40)
                for i in range(shnum)]
    string_table = sections[name_index]
    if string_table[1] != 3 or string_table[4] > len(data) or string_table[5] > len(data) - string_table[4]:
        raise ValueError('invalid ELF section names')
    names = data[string_table[4]:string_table[4]+string_table[5]]
    def section_name(section):
        offset = section[0]
        if offset >= len(names) or b'\0' not in names[offset:]:
            raise ValueError('unterminated ELF section name')
        return names[offset:].split(b'\0', 1)[0].decode('ascii')
    mapped = []
    orphan_exec = []
    for section in sections:
        name = section_name(section)
        _, kind, flags, virtual, file_offset, size, *_ = section
        if kind != SHT_NOBITS and (file_offset > len(data) or size > len(data)-file_offset):
            raise ValueError('ELF section extends past file')
        if flags & SHF_ALLOC and name in MAPPED:
            if virtual + size > (1 << 32):
                raise ValueError('mapped section address overflow')
            mapped.append((virtual, virtual+size))
        elif flags & SHF_ALLOC and flags & SHF_EXECINSTR and size:
            orphan_exec.append(name)
    if not any(section_name(s) == '.text' and s[5] for s in sections):
        raise ValueError('missing runtime-mapped .text')
    unmapped = []
    relocations = 0
    for section in sections:
        if section[1] != SHT_RELA:
            continue
        if section[5] % 12:
            raise ValueError('malformed RELA size')
        for j in range(section[5] // 12):
            offset, info, _ = struct.unpack_from('<IIi', data, section[4]+j*12)
            relocations += 1
            if not any(lo <= offset and offset+4 <= hi for lo,hi in mapped):
                unmapped.append({'section': section_name(section),
                                 'offset': hex(offset), 'type': info & 255})
    return {'relocations_examined': relocations,
            'unmapped_relocations': unmapped,
            'unmapped_executable_sections': sorted(set(orphan_exec))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    result = audit_loader_map(args.elf)
    if result['unmapped_relocations'] or result['unmapped_executable_sections']:
        for item in result['unmapped_relocations'][:10]:
            print('UNMAPPED RELA:', item)
        print('UNMAPPED executable sections:', result['unmapped_executable_sections'])
        parser.exit(1, f"Provider ELF loader-map failure: {len(result['unmapped_relocations'])} relocations cannot be applied\n")
    print(f"Provider ELF loader mapping: PASS ({result['relocations_examined']} relocation sites)")


if __name__ == '__main__':
    main()
