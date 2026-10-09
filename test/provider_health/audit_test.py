#!/usr/bin/env python3
"""Mutate real target artifacts to verify the selected-profile validation."""
from pathlib import Path
import struct
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from audit_provider_health_elf import audit
from elftools.elf.elffile import ELFFile
for arg in sys.argv[1:]:
    path=Path(arg);audit(path)
    original=path.read_bytes()
    with path.open('rb') as f:
        elf=ELFFile(f);dyn=elf.get_section_by_name('.dynsym')
        i,sym=next((i,s) for i,s in enumerate(dyn.iter_symbols()) if s.name=='risc_provider_health_v1_descriptor')
        section=elf.get_section(sym['st_shndx']);address=sym['st_value']
        offset=section['sh_offset']+address-section['sh_addr']
        symbol=dyn['sh_offset']+i*dyn['sh_entsize']
        header=elf.header['e_shoff']+sym['st_shndx']*elf.header['e_shentsize']
        relocation=next(s['sh_offset']+i*s['sh_entsize'] for s in elf.iter_sections() if s['sh_type']=='SHT_RELA' for i,r in enumerate(s.iter_relocations()) if r['r_offset']==address+8)
        data_address=elf.get_section_by_name('.data')['sh_addr']
        names=elf.get_section(elf.header['e_shstrndx']).data()
        data_name=names.index(b'.data\0')
        dynstr=elf.get_section(dyn['sh_link'])
        name_offset=dynstr['sh_offset']+sym['st_name']
    mutations=[('version',offset,2),('prefix-size',offset+4,8),('foreign-callback',offset+8,data_address),
               ('object-size',symbol+8,8),('writable-section',header,data_name),
               ('nonrelative-callback',relocation+4,4),('foreign-relative-symbol',relocation+4,0x105),
               ('missing-symbol',name_offset,int.from_bytes(b'xisc','little'))]
    with tempfile.TemporaryDirectory() as td:
        out=Path(td)/'mutated.elf'
        for label,at,value in mutations:
            copy=bytearray(original);struct.pack_into('<I',copy,at,value);out.write_bytes(copy)
            try:audit(out)
            except ValueError:continue
            raise AssertionError('Accepted '+label)
    print(path.name+': readonly own-descriptor/size/version/local callback/relocation negative controls PASS (8)')
