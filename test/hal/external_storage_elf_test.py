#!/usr/bin/env python3
"""Reject a linked resident Arduino SD/SDFS/SdFat owner in Reader firmware."""
from pathlib import Path
import hashlib,sys
from elftools.elf.elffile import ELFFile
if len(sys.argv)!=2: raise SystemExit('Pass one actual Reader firmware ELF')
path=Path(sys.argv[1])
if path.stat().st_size>256*1024*1024: raise ValueError('Firmware ELF exceeds audit bound')
with path.open('rb') as stream:
 elf=ELFFile(stream)
 assert elf['e_machine']=='EM_XTENSA' and elf['e_type']=='ET_EXEC'
 table=elf.get_section_by_name('.symtab');assert table and table.num_symbols()<1000000
 names={symbol.name for symbol in table.iter_symbols() if symbol['st_shndx']!='SHN_UNDEF'}
 forbidden=sorted(name for name in names if name=='SD' or any(token in name for token in ('4SDFS','4SdFs','5SdFat')))
 assert not forbidden, 'Resident raw SD owner: '+repr(forbidden)
 stream.seek(0);digest=hashlib.sha256()
 for block in iter(lambda:stream.read(1024*1024),b''):digest.update(block)
 print('No linked SD/SDFS/SdFat owner: PASS sha256='+digest.hexdigest())
