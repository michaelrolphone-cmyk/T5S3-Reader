#!/usr/bin/env python3
"""Inspect actual local/released ELF bytes for retired direct-storage imports.

Read-only. Never installs, changes or executes any package. Accepts .elf files
and ordinary ZIP archives; records hashes so results cannot imply another build.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import zipfile
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[1]
RETIRED=set(re.findall(r'^RISC_RETIRED_STORAGE_IMPORT\((\w+)\)',
    (ROOT/'lib/NativeApps/include/RetiredStorageImports.def').read_text(),re.M))


def inspect(data,name):
    if not 52<=len(data)<=8*1024*1024: raise ValueError('ELF size outside audit bound')
    elf=ELFFile(io.BytesIO(data));imports=set()
    for section in elf.iter_sections():
        if section['sh_type'] not in ('SHT_SYMTAB','SHT_DYNSYM'): continue
        for symbol in section.iter_symbols():
            if symbol.name and symbol['st_shndx']=='SHN_UNDEF': imports.add(symbol.name)
    return dict(file=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),
                retired_imports=sorted(imports&RETIRED),imports=sorted(imports))


def audit(path):
    data=path.read_bytes()
    if len(data)>16*1024*1024: raise ValueError('Archive size outside audit bound')
    record=dict(file=str(path),sha256=hashlib.sha256(data).hexdigest(),elfs=[])
    if path.suffix=='.elf': record['elfs'].append(inspect(data,path.name))
    else:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            members=archive.infolist()
            if len(members)>128: raise ValueError('Too many package members')
            for entry in members:
                if not entry.filename.endswith('.elf'): continue
                if entry.file_size>8*1024*1024: raise ValueError('Oversized packaged ELF')
                record['elfs'].append(inspect(archive.read(entry),entry.filename))
    if not record['elfs']: raise ValueError(f'No ELF in {path}')
    return record


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files',type=Path,nargs='+')
    args=parser.parse_args()
    if len(args.files)>128: raise ValueError('Too many audit inputs')
    records=[audit(path) for path in args.files]
    print(json.dumps(records,indent=2))
