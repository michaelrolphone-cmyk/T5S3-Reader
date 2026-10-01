#!/usr/bin/env python3
"""Compile actual esp_elf_open and its fallback; probe owned-buffer admission."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'lib/elf_loader/src/esp_elf.c').read_text()
start=source.index('__attribute__((weak)) bool esp_elf_admit_managed_app')
end=source.index('/**',source.index('int esp_elf_open(',start))
body=source[start:end]
header=r'''
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#define FS_PATH "/storage"
#define TAG "ELF"
#define ESP_LOGE(...) ((void)0)
typedef struct { uint8_t* payload; size_t size; } elf_file_t;
int fake_open(const char*,int);ssize_t fake_read(int,void*,size_t);
off_t fake_lseek(int,off_t,int);int fake_close(int);
void* esp_elf_malloc(size_t,bool);void esp_elf_free(void*);
#define open fake_open
#define read fake_read
#define lseek fake_lseek
#define close fake_close
'''
fixture=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
typedef struct {uint8_t* payload;size_t size;} elf_file_t;
int esp_elf_open(elf_file_t*,const char*);
static unsigned allocations,frees,closes,admissions;
static int fault;
static uint8_t disk[64];
int fake_open(const char* path,int flags){(void)flags;assert(path);return fault==1?-1:7;}
ssize_t fake_read(int fd,void* out,size_t count){assert(fd==7&&count==sizeof(disk));
 memcpy(out,disk,count);disk[0]^=1;return fault==2?1:(ssize_t)count;}
off_t fake_lseek(int fd,off_t at,int whence){assert(fd==7);return whence==SEEK_END?(off_t)sizeof(disk):at;}
int fake_close(int fd){assert(fd==7);++closes;return fault==4?-1:0;}
void* esp_elf_malloc(size_t count,bool exec){(void)exec;++allocations;return malloc(count);}
void esp_elf_free(void* p){++frees;free(p);}
bool esp_elf_validate_file(const uint8_t* bytes,size_t count){return bytes&&count==sizeof(disk)&&fault!=3;}
#ifndef FALLBACK_ONLY
bool esp_elf_admit_managed_app(const char* path,const uint8_t* bytes,size_t count){
 ++admissions;assert(path&&bytes!=disk&&count==sizeof(disk));assert(bytes[0]==0x42&&disk[0]!=0x42);
 assert(closes==1);return fault!=5;}
#endif
int main(void){
 for(fault=0;fault<=5;++fault){
  memset(disk,0x42,sizeof(disk));allocations=frees=closes=admissions=0;elf_file_t file={0};
  int result=esp_elf_open(&file,"/sd/Apps/test/test.elf");
#ifdef FALLBACK_ONLY
  assert(result<0);assert(frees==allocations);
#else
  if(!fault){assert(result==0&&admissions==1&&file.size==64);esp_elf_free(file.payload);}
  else {assert(result<0&&frees==allocations);assert(admissions==(fault==5?1u:0u));}
#endif
  assert(closes==(fault==1?0u:1u));
 }
#ifdef FALLBACK_ONLY
 fault=0;memset(disk,0x42,sizeof(disk));elf_file_t file={0};
 assert(esp_elf_open(&file,"/sd/Apps/test.elf")==0);esp_elf_free(file.payload);
#endif
 puts("Actual esp_elf_open: owned copied bytes, checked close, strong admission, failure cleanup and fail-closed canonical fallback PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='u1-owned-elf-') as temp:
    path=Path(temp);(path/'open.c').write_text(header+body);(path/'test.c').write_text(fixture)
    for fallback in (False,True):
        command=['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
        if fallback:command+=['-DFALLBACK_ONLY']
        subprocess.run(command+[str(path/'open.c'),str(path/'test.c'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True,env=os.environ.copy())
