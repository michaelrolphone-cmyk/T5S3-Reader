#!/usr/bin/env python3
"""Actual app import rejection before normal/module fallback; no hardware."""
from pathlib import Path
import re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
compat=(ROOT/'src/native/NativeHardwareCompat.cpp').read_text()
loader=(ROOT/'lib/elf_loader/src/esp_elf_symbol.c').read_text()
retired=re.findall(r'^RISC_RETIRED_STORAGE_IMPORT\((\w+)\)',
                  (ROOT/'lib/NativeApps/include/RetiredStorageImports.def').read_text(),re.M)
retired+=re.findall(r'^RISC_RETIRED_DISPLAY_IMPORT\((\w+)\)',
                  (ROOT/'lib/NativeApps/include/RetiredDisplayImports.def').read_text(),re.M)
exports=set(re.findall(r'^RISC_COMPAT_SYMBOL\((\w+)\)',
                      (ROOT/'lib/NativeApps/include/NativeHardwareCompatSymbols.def').read_text(),re.M))
assert retired and not set(retired)&exports

def function(source,signature):
    start=source.index(signature);opening=source.index('{',start);depth=0
    for at in range(opening,len(source)):
        depth+=(source[at]=='{')-(source[at]=='}')
        if depth==0:return source[start:at+1]
    raise AssertionError('Unterminated function')

prefix=r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#define ESP_LOGE(...) ((void)0)
#define LOG_ERR(...) ((void)0)
typedef struct {const char*name;const void*sym;} esp_elf_symbol_table_t;
static esp_elf_symbol_table_t g_esp_libc_elfsyms[]={{0,0}};
static esp_elf_symbol_table_t g_esp_espidf_elfsyms[]={{0,0}};
static bool esp_elf_privileged_os_cpu_scope_owned_v1(void){return false;}
static uintptr_t esp_elf_privileged_os_cpu_lookup_v1(const char*n){(void)n;return 0;}
static uintptr_t native_app_memory_symbol(const char*n){(void)n;return 0x1234;}
static uintptr_t esp_elf_find_symbol(const char*n){(void)n;return 0x5678;}
extern bool native_app_import_allowed(const char*) __attribute__((weak));
'''
policy='''#include <cstring>\n#define LOG_ERR(...) ((void)0)\nstatic bool retiredStorageImport=false,retiredDisplayImport=false;\n'''
policy+=function(compat,'extern "C" bool native_app_import_allowed')
policy+=function(compat,'extern "C" const char* native_hardware_compat_last_error')
policy+=function(compat,'extern "C" void native_hardware_compat_clear_error')
main='int main(void){\n'
for name in retired: main+=f'assert(elf_find_sym_default("{name}")==0);\n'
main+='assert(elf_find_sym_default("t5_storage_get_api")==0x1234);\n'
main+='assert(elf_find_sym_default("t5_video_get_api")==0x1234);\n'
main+='assert(elf_find_sym_default("SD_other")==0x1234);return 0;}\n'
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);c=tmp/'resolve.c';cpp=tmp/'policy.cpp'
    c.write_text(prefix+function(loader,'uintptr_t elf_find_sym_default')+main)
    cpp.write_text(policy)
    obj=tmp/'resolve.o';binary=tmp/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-c',str(c),'-o',str(obj)],check=True)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT/'lib/NativeApps/include'),
                    str(cpp),str(obj),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Retired SD/SDFS/LCD imports rejected before allocator/table/module fallback; shared storage API preserved: PASS')
