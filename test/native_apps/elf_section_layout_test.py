#!/usr/bin/env python3
"""Exercise actual production section mapper and Xtensa relocation writes.

Linux low-address allocations stand in for ESP byte-addressable heaps. No guest
code is executed. Optional real ELF arguments use this same compiled fixture.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]

def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for at in range(opening, len(source)):
        depth += (source[at] == '{') - (source[at] == '}')
        if not depth:
            return source[start:at + 1]
    raise AssertionError('unterminated production function')

loader = (ROOT / 'lib/elf_loader/src/esp_elf.c').read_text()
arch = (ROOT / 'lib/elf_loader/src/arch/esp_elf_xtensa.c').read_text()
prefix = r'''
#define _GNU_SOURCE
#define CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR 1
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "esp_elf.h"
#include "private/esp_elf_data_layout.h"
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define stype(s,t) ((s)->type==(t))
#define sflags(s,f) (((s)->flags&(f))==(f))
#define R_XTENSA_RTLD 2
#define R_XTENSA_GLOB_DAT 3
#define R_XTENSA_JMP_SLOT 4
#define R_XTENSA_RELATIVE 5
static bool privileged_peripheral_address(uint32_t value){(void)value;return false;}
static unsigned allocations, frees, fail_at, skew;
#ifdef CONFIG_ELF_LOADER_SET_MMU
static bool fail_mmu;
static int esp_elf_arch_init_mmu(esp_elf_t* elf){(void)elf;return fail_mmu;}
#endif
struct allocation {uint8_t* mapping; uint8_t* pointer; size_t bytes, mapped;};
static struct allocation owned[2];
static void* esp_elf_malloc(uint32_t size, bool executable){
 ++allocations;if(allocations==fail_at)return NULL;
 assert(size && size < 8u*1024u*1024u && allocations<=2);
 size_t prefix=64+(executable?0:skew), mapped=(size+prefix+64+4095)&~4095u;
 uint8_t* memory=mmap(NULL,mapped,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
 assert(memory!=MAP_FAILED && (uintptr_t)memory+mapped<=UINT32_MAX);
 memset(memory,0xa5,mapped);
 owned[allocations-1]=(struct allocation){memory,memory+prefix,size,mapped};
 return memory+prefix;
}
static void esp_elf_free(void* pointer){
 if(!pointer)return;
 bool found=false;
 for(unsigned i=0;i<2;++i)if(owned[i].pointer==pointer){
  for(uint8_t* p=owned[i].mapping;p<owned[i].pointer;++p)assert(*p==0xa5);
  for(uint8_t* p=owned[i].pointer+owned[i].bytes;p<owned[i].mapping+owned[i].mapped;++p)assert(*p==0xa5);
  assert(!munmap(owned[i].mapping,owned[i].mapped));owned[i].pointer=NULL;++frees;found=true;break;
 }
 assert(found);
}
'''
# Use the real map helper, including its exact virtual ranges.
mapper = function(loader, 'uintptr_t esp_elf_map_sym(') if 'uintptr_t esp_elf_map_sym(' in loader else ''
if not mapper:
    mapper = function(arch, 'uintptr_t esp_elf_map_sym(')
body = function(loader, 'static int esp_elf_load_section(')
relocate = function(arch, 'int esp_elf_arch_relocate(')
suffix = r'''
static void clear(esp_elf_t* elf){esp_elf_free(elf->pdata);esp_elf_free(elf->ptext);memset(elf,0,sizeof(*elf));}
static unsigned section_id(const char* name){
 if(!strcmp(name,".text"))return ELF_SEC_TEXT;
 if(!strcmp(name,".data"))return ELF_SEC_DATA;
 if(!strcmp(name,".rodata"))return ELF_SEC_RODATA;
 if(!strcmp(name,".data.rel.ro"))return ELF_SEC_DRLRO;
 if(!strcmp(name,".bss"))return ELF_SEC_BSS;
 return ELF_SECS;
}
int main(int argc,char** argv){
 assert(argc==2);
 FILE* input=fopen(argv[1],"rb");assert(input);assert(!fseek(input,0,SEEK_END));long length=ftell(input);rewind(input);
 uint8_t* data=malloc(length);assert(data&&fread(data,1,length,input)==(size_t)length);fclose(input);
 const elf32_hdr_t* header=(const elf32_hdr_t*)data;
 const elf32_shdr_t* sections=(const elf32_shdr_t*)(data+header->shoff);
 const char* names=(const char*)data+sections[header->shstrndx].offset;
 unsigned total_relocations=0;
 for(skew=0;skew<16;++skew){
  esp_elf_t elf={0};allocations=frees=fail_at=0;
  assert(!esp_elf_load_section(&elf,data));
  for(unsigned i=0;i<header->shnum;++i){
   unsigned id=section_id(names+sections[i].name);
   if(id==ELF_SECS||!sections[i].size)continue;
   const uint8_t* mapped=(const uint8_t*)elf.sec[id].addr;
   assert(elf.sec[id].size==sections[i].size && elf.sec[id].v_addr==sections[i].addr);
   if(id!=ELF_SEC_TEXT){
    assert((elf.sec[id].addr&3)==(sections[i].addr&3));
    assert(!sections[i].addralign||!(elf.sec[id].addr&(sections[i].addralign-1)));
    assert(mapped>=elf.pdata&&mapped+sections[i].size<=elf.pdata+owned[1].bytes);
   }
   if(id==ELF_SEC_BSS){for(unsigned n=0;n<sections[i].size;++n)assert(!mapped[n]);}
   else assert(!memcmp(mapped,data+sections[i].offset,sections[i].size));
  }
  for(unsigned i=0;i<header->shnum;++i)if(sections[i].type==SHT_RELA){
   assert(!(sections[i].size%sizeof(elf32_rela_t)));
   for(unsigned n=0;n<sections[i].size/sizeof(elf32_rela_t);++n){
    elf32_rela_t rel;memcpy(&rel,data+sections[i].offset+n*sizeof(rel),sizeof(rel));
    uintptr_t where=esp_elf_map_sym(&elf,rel.offset);assert(where&&!(where&3));
    assert(!esp_elf_arch_relocate(&elf,&rel,NULL,0x12345678));++total_relocations;
   }
  }
  // A corrupt/legacy mapping is rejected before a uint32_t dereference.
  for(unsigned i=0;i<ELF_SECS;++i)if(elf.sec[i].size>=4){
    uintptr_t saved_address=elf.sec[i].addr;
    const uintptr_t virtual_site=(elf.sec[i].v_addr+3u)&~(uintptr_t)3u;
    if(virtual_site+4<=elf.sec[i].v_addr+elf.sec[i].size){
      elf.sec[i].addr=saved_address+1;
      elf32_rela_t rel={(uint32_t)virtual_site,R_XTENSA_GLOB_DAT,0};
      assert(esp_elf_arch_relocate(&elf,&rel,NULL,0x12345678)==-EINVAL);
      elf.sec[i].addr=saved_address;
    }
  }
  clear(&elf);assert(frees==allocations);
 }
 for(fail_at=1;fail_at<=2;++fail_at){
  esp_elf_t elf={0};allocations=frees=0;
  assert(esp_elf_load_section(&elf,data)==-ENOMEM);
  assert(!elf.ptext&&!elf.pdata&&!elf.entry);
  for(unsigned i=0;i<ELF_SECS;++i)assert(!elf.sec[i].addr);
  clear(&elf);assert(frees==fail_at-1);
 }
 // Reject malformed layout metadata before either allocation occurs.
 unsigned dr=0,ro=0;
 for(unsigned i=0;i<header->shnum;++i){
  const unsigned id=section_id(names+sections[i].name);
  if(id==ELF_SEC_DRLRO)dr=i;
  if(id==ELF_SEC_RODATA)ro=i;
 }
 assert(dr&&ro);
 elf32_shdr_t* writable=(elf32_shdr_t*)(data+header->shoff);
 const elf32_shdr_t saved=writable[dr], saved_ro=writable[ro];
 for(unsigned mode=0;mode<6;++mode){
  writable[dr]=saved;writable[ro]=saved_ro;
  if(mode==0)writable[dr].addralign=3;
  if(mode==1){writable[dr].addralign=16;writable[dr].addr|=1;}
  if(mode==2)writable[dr].size=UINT32_MAX;
  if(mode==3)writable[dr].addr=header->entry;
  if(mode==4)writable[ro].name=writable[dr].name;
  if(mode==5){writable[dr].addralign=0x80000000u;writable[dr].addr=0;}
  esp_elf_t elf={0};allocations=frees=fail_at=0;
  assert(esp_elf_load_section(&elf,data)==-EINVAL);
  assert(!allocations&&!frees&&!elf.ptext&&!elf.pdata);
 }
 writable[dr]=saved;writable[ro]=saved_ro;
#ifdef CONFIG_ELF_LOADER_SET_MMU
 {esp_elf_t elf={0};allocations=frees=fail_at=0;fail_mmu=true;
  assert(esp_elf_load_section(&elf,data)==-EIO);
  assert(!elf.ptext&&!elf.pdata&&!elf.entry&&frees==2);
  for(unsigned i=0;i<ELF_SECS;++i)assert(!elf.sec[i].addr);
  clear(&elf);assert(frees==2);fail_mmu=false;}
#endif
 uint32_t cap=0,out=0;
 assert(!esp_elf_data_reserve(UINT32_MAX,4,0,&cap)&&!cap);
 assert(!esp_elf_data_reserve(1,3,0,&cap));
 assert(!esp_elf_data_reserve(1,8,4,&cap));
 assert(esp_elf_data_reserve(33,1,2,&cap)&&cap==36);
 assert(!esp_elf_data_place(0,0,32,33,1,2,&out));
 assert(!esp_elf_data_place(UINTPTR_MAX-1,4,64,8,4,0,&out));
 assert(!esp_elf_data_place(UINTPTR_MAX-1,0,64,8,1,2,&out));
 free(data);printf("Production section mapper: 16 allocation residues, %u relocation writes, contents/BSS, guards, overflow and allocation cleanup PASS\n",total_relocations);
}
'''

def fixture():
    names=b'\0.text\0.data\0.rodata\0.data.rel.ro\0.bss\0.rela.dyn\0.shstrtab\0'
    data=bytearray(256)
    sections=[(0,0,0,0,0,0,0,0,0,0)]
    layouts=[('.text',1,6,0x400,bytes(64),4),('.data',1,3,0x2000,bytes(5),8),
             ('.rodata',1,2,0x2102,bytes(range(33)),1),('.data.rel.ro',1,3,0x2200,struct.pack('<I',0x400)+bytes(52),16),
             ('.bss',8,3,0x2400,bytes(7),64)]
    for name,kind,flags,virtual,payload,alignment in layouts:
        offset=len(data)
        if kind!=8:data.extend(payload)
        sections.append((names.index(name.encode()+b'\0'),kind,flags,virtual,offset,len(payload),0,0,alignment,0))
    offset=len(data);rel=struct.pack('<IIi',0x2200,5,0);data.extend(rel)
    sections.append((names.index(b'.rela.dyn\0'),4,0,0,offset,len(rel),0,0,4,12))
    offset=len(data);data.extend(names)
    sections.append((names.index(b'.shstrtab\0'),3,0,0,offset,len(names),0,0,1,0))
    data.extend(bytes((-len(data))%4));shoff=len(data)
    for s in sections:data.extend(struct.pack('<10I',*s))
    ident=b'\x7fELF\x01\x01\x01'+bytes(9)
    data[:52]=ident+struct.pack('<HHIIIIIHHHHHH',3,94,1,0x400,0,shoff,0,52,0,0,40,len(sections),len(sections)-1)
    return data

with tempfile.TemporaryDirectory(prefix='elf-section-layout-') as temp:
    p=Path(temp);src=p/'layout.c';exe=p/'layout';sample=p/'sample.elf'
    src.write_text(prefix+mapper+body+relocate+suffix);sample.write_bytes(fixture())
    (p/'sdkconfig.h').write_text('#pragma once\n')
    for extra in ([], ['-DCONFIG_ELF_LOADER_SET_MMU=1']):
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                        '-Wno-unused-parameter','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast','-fsanitize=undefined',
                        '-fno-sanitize-recover=all',*extra,'-I'+str(p),'-I'+str(ROOT/'lib/elf_loader/include'),str(src),'-o',str(exe)],check=True)
        for path in [sample,*map(Path,sys.argv[1:])]:
            subprocess.run([str(exe),str(path)],check=True)
