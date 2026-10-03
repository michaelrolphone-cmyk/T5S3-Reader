#!/usr/bin/env python3
"""Preserve real relocations while removing the observed display linker NOPs."""
from pathlib import Path
import struct,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from normalize_xtensa_relocations import normalize
original=(ROOT/'dist/experimental/display-epd-video/driver.elf').read_bytes()
shoff=struct.unpack_from('<I',original,32)[0];count=struct.unpack_from('<H',original,48)[0]
name_index=struct.unpack_from('<H',original,50)[0]
sections=[struct.unpack_from('<10I',original,shoff+i*40) for i in range(count)]
names_header=sections[name_index];names=original[names_header[4]:names_header[4]+names_header[5]]
indices={names[s[0]:].split(b'\0',1)[0].decode():i for i,s in enumerate(sections)}
ri=indices['.rela.dyn'];rela=sections[ri];plt=sections[indices['.rela.plt']];dynamic=sections[indices['.dynamic']]
assert plt[4]-rela[4]-rela[5]==24,'Expected the two normalized display placeholders'
records=[original[i:i+12] for i in range(rela[4],rela[4]+rela[5],12)]
rtld=[i for i,r in enumerate(records) if (struct.unpack_from('<I',r,4)[0]&255)==2]
assert len(rtld)==4
size_entry=next(i for i in range(dynamic[4],dynamic[4]+dynamic[5],8) if struct.unpack_from('<I',original,i)[0]==8)

def image(split):
 data=bytearray(original);new=list(records)
 if split:
  new.insert(rtld[-1]+1,bytes(12));new.insert(rtld[0],bytes(12))
 else:new[rtld[-1]+1:rtld[-1]+1]=[bytes(12),bytes(12)]
 data[rela[4]:plt[4]]=b''.join(new)
 struct.pack_into('<I',data,shoff+ri*40+20,rela[5]+24)
 struct.pack_into('<I',data,size_entry+4,rela[5]+24)
 return data
with tempfile.TemporaryDirectory() as d:
 path=Path(d)/'provider.elf'
 for split in (False,True):
  raw=image(split);path.write_bytes(raw)
  assert normalize(path)==0 and path.read_bytes()==raw,'Other build profiles must not accept the new pattern'
  assert normalize(path,display_gcc14_noops=True)==2
  fixed=path.read_bytes();assert fixed[rela[4]:rela[4]+rela[5]]==b''.join(records)
  assert struct.unpack_from('<I',fixed,size_entry+4)[0]==rela[5]
  assert struct.unpack_from('<I',fixed,shoff+ri*40+20)[0]==rela[5]
  malformed=image(split);zero=next(i for i in range(rela[4],plt[4],12) if malformed[i:i+12]==bytes(12))
  struct.pack_into('<I',malformed,zero+8,1) # Nonzero addend is not a placeholder.
  path.write_bytes(malformed)
  try:assert normalize(path,display_gcc14_noops=True)==0
  except ValueError:pass
  assert path.read_bytes()==malformed,'Malformed record must not be repaired'
print('Display linker placeholders: both object orders preserve every real relocation; default/malformed patterns unchanged PASS')
