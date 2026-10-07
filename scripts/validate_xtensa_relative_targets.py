"""Check generated PIC relative pointers against the sections the loader maps.

The runtime rejects a relative pointer whose value is outside those sections.
Link-time loop transforms can create such pre-biased pointers even in otherwise
valid C. Detect that before sending an image to hardware; do not relax the loader.
"""
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile


def validate(path):
    path = Path(path)
    if not 52 <= path.stat().st_size <= 256 * 1024:
        raise ValueError('Provider ELF size outside bound')
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_XTENSA':
            raise ValueError('Expected little-endian Xtensa ELF32')
        sections = list(elf.iter_sections())
        if not 1 <= len(sections) <= 256:
            raise ValueError('Section count outside bound')
        mapped = [s for s in sections if s.name in ('.text', '.data', '.rodata', '.data.rel.ro', '.bss')]
        count = 0
        for section in sections:
            if section['sh_type'] != 'SHT_RELA':
                continue
            if section.num_relocations() > 16384:
                raise ValueError('Relocation count outside bound')
            for relocation in section.iter_relocations():
                if relocation['r_info_type'] != 5:  # R_XTENSA_RELATIVE
                    continue
                offset = relocation['r_offset']
                owner = next((s for s in mapped if s['sh_type'] == 'SHT_PROGBITS'
                              and s['sh_addr'] <= offset and offset + 4 <= s['sh_addr'] + s['sh_size']), None)
                if owner is None or offset % 4:
                    raise ValueError(f'Unmapped relative write at {offset:#x}')
                value = struct.unpack_from('<I', owner.data(), offset - owner['sh_addr'])[0]
                if value and not any(s['sh_addr'] <= value < s['sh_addr'] + s['sh_size'] for s in mapped):
                    raise ValueError(f'Unmappable relative target {value:#x} at {offset:#x}')
                count += 1
        return count


if __name__ == '__main__':
    import sys
    for argument in sys.argv[1:]:
        print(argument, 'relative pointers verified:', validate(argument))
