#!/usr/bin/env python3
"""Remove linker-emitted trailing R_XTENSA_NONE entries from a PIC ELF.

Xtensa binutils 2.35 sometimes appends null relocation slots to .rela.dyn
for imported OS symbols. RiscRTE deliberately rejects relocation type NONE
rather than silently accepting arbitrary records. This utility changes only
.rela.dyn's section size and DT_RELASZ to exclude *trailing all-zero* entries;
the original bytes remain inert padding, the following section, code, addresses,
program headers, and actual relocation entries are never moved or modified.
Fail closed if this precise layout or its metadata differs.
"""
from pathlib import Path
import struct


def normalize(path):
    path = Path(path)
    data = bytearray(path.read_bytes())
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Not a little-endian 32-bit ELF')

    def u16(pos):
        return struct.unpack_from('<H', data, pos)[0]

    def u32(pos):
        return struct.unpack_from('<I', data, pos)[0]

    if u16(16) != 3 or u16(18) != 94:  # ET_DYN / Xtensa
        raise ValueError('Only Xtensa shared objects are supported')
    shoff, shentsize, shnum, shstr = u32(32), u16(46), u16(48), u16(50)
    if shentsize != 40 or not 1 <= shnum <= 256 or shstr >= shnum or \
            shoff + shnum * shentsize > len(data):
        raise ValueError('Invalid ELF section table')

    def section(i):
        return shoff + i * shentsize

    strings = section(shstr)
    if u32(strings + 4) != 3 or u32(strings + 16) + u32(strings + 20) > len(data):
        raise ValueError('Invalid section names')
    names_off, names_len = u32(strings + 16), u32(strings + 20)
    sections = {}
    for i in range(shnum):
        name_idx = u32(section(i))
        if name_idx >= names_len:
            raise ValueError('Invalid section name index')
        end = data.find(b'\0', names_off + name_idx, names_off + names_len)
        if end < 0:
            raise ValueError('Unterminated section name')
        name = bytes(data[names_off + name_idx:end]).decode('ascii')
        if name in sections:
            raise ValueError('Duplicate ELF section name: ' + name)
        sections[name] = section(i)
    for name in ('.rela.dyn', '.rela.plt', '.dynamic'):
        if name not in sections:
            raise ValueError('Missing required section: ' + name)
    rela, plt, dynamic = (sections[name] for name in
                          ('.rela.dyn', '.rela.plt', '.dynamic'))
    start, size = u32(rela + 16), u32(rela + 20)
    dyn_start, dyn_size = u32(dynamic + 16), u32(dynamic + 20)
    if (u32(rela + 4) != 4 or size % 12 or start + size > len(data) or
            start + size != u32(plt + 16) or u32(plt + 4) != 4 or
            u32(dynamic + 4) != 6 or dyn_size % 8 or
            dyn_start + dyn_size > len(data)):
        raise ValueError('Unexpected relocation/dynamic layout')
    count = size // 12
    trailing = 0
    while trailing < count and data[start + (count - trailing - 1) * 12:
                                     start + (count - trailing) * 12] == b'\0' * 12:
        trailing += 1
    if not trailing:
        return 0  # Some toolchains already emit canonical relocation tables.
    if trailing > 8 or trailing == count:
        raise ValueError('Unexpected quantity of no-op relocations')
    for offset in range(start, start + size - trailing * 12, 12):
        reloc_addr, reloc_info, _addend = struct.unpack_from('<IIi', data, offset)
        if not reloc_addr or reloc_addr % 4 or (reloc_info & 255) not in (2, 3, 4, 5):
            raise ValueError('Nontrailing or unsupported relocation present')
    relasz_entries = [offset for offset in range(dyn_start, dyn_start + dyn_size, 8)
                      if u32(offset) == 8]  # DT_RELASZ
    if len(relasz_entries) != 1 or u32(relasz_entries[0] + 4) != size:
        raise ValueError('DT_RELASZ is inconsistent with .rela.dyn')
    new_size = size - trailing * 12
    struct.pack_into('<I', data, rela + 20, new_size)
    struct.pack_into('<I', data, relasz_entries[0] + 4, new_size)
    path.write_bytes(data)
    return trailing


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    print('Excluded linker-emitted trailing R_XTENSA_NONE entries:',
          normalize(args.elf))
