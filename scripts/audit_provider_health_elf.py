#!/usr/bin/env python3
"""Check exact ELF32 health object, readonly storage and local callback relocation."""
import argparse
import json
from pathlib import Path
import struct

def audit(path):
    data=Path(path).read_bytes()
    if data[:7]!=b'\x7fELF\x01\x01\x01' or len(data)<52: raise ValueError('Expected little-endian ELF32')
    h=struct.unpack_from('<HHIIIIIHHHHHH',data,16)
    if h[0]!=3 or h[1]!=94 or h[10]!=40: raise ValueError('Expected Xtensa ET_DYN')
    sections=[struct.unpack_from('<IIIIIIIIII',data,h[5]+i*40) for i in range(h[11])]
    strings=sections[h[12]];names=data[strings[4]:strings[4]+strings[5]]
    def name(s):return names[s[0]:].split(b'\0',1)[0].decode()
    exports=[];found=[]
    for s in sections:
        if s[1]!=11:continue
        st=sections[s[6]];ss=data[st[4]:st[4]+st[5]]
        for at in range(s[4],s[4]+s[5],s[9]):
            n,v,size,info,other,idx=struct.unpack_from('<IIIBBH',data,at)
            symbol=ss[n:].split(b'\0',1)[0].decode()
            if idx and info>>4==1 and other&3==0: exports.append(symbol)
            if symbol=='risc_provider_health_v1_descriptor':found.append((v,size,info,other,idx))
    if set(exports)-{'t5_driver_get','risc_provider_health_v1_descriptor','__bss_start','_edata','_end'}:
        raise ValueError('Unexpected exports: '+repr(exports))
    if len(found)!=1: raise ValueError('Exactly one dynamic descriptor is required')
    value,size,info,other,index=found[0]
    if info!=0x11 or other&3 or size!=12 or not 0<index<len(sections):raise ValueError('Invalid exported object')
    s=sections[index];section=name(s)
    if section not in ('.rodata','.data.rel.ro') or (section=='.rodata' and s[2]&1) or not s[2]&2:
        raise ValueError('Descriptor is not readonly mapped storage')
    if not s[3]<=value or value+size>s[3]+s[5]:raise ValueError('Descriptor extent is outside mapping')
    version,declared,callback=struct.unpack_from('<III',data,s[4]+value-s[3])
    if (version,declared)!=(1,12):raise ValueError('Descriptor ABI prefix differs')
    text=next(x for x in sections if name(x)=='.text')
    if not text[2]&4 or not text[3]<=callback<text[3]+text[5]:raise ValueError('Foreign/nonexecutable callback')
    relocations=[]
    for r in sections:
        if r[1]!=4:continue
        for at in range(r[4],r[4]+r[5],r[9]):
            offset,kind,addend=struct.unpack_from('<IIi',data,at)
            if offset==value+8:relocations.append((kind&255,kind>>8,addend))
    if len(relocations)!=1 or relocations[0][:2]!=(5,0):raise ValueError('Callback must have one local RELATIVE relocation')
    return {'descriptor_symbol':'risc_provider_health_v1_descriptor','section':section,'object_bytes':size,
            'api_version':version,'callback':hex(callback),'callback_relocations':relocations,'exports':exports}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf',type=Path)
    print(json.dumps(audit(p.parse_args().elf),indent=2))
