#!/usr/bin/env python3
"""Exercise production registered lookup + compatibility lifecycle, not hardware."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
loader=(ROOT/'lib/elf_loader/src/esp_elf.c').read_text()
compat=(ROOT/'src/native/NativeHardwareCompat.cpp').read_text()
inventory=(ROOT/'lib/NativeApps/include/NativeHardwareCompatSymbols.def').read_text()
names=re.findall(r'^RISC_COMPAT_SYMBOL\((\w+)\)',inventory,re.M)
assert names and compat.count('#include "NativeHardwareCompatSymbols.def"')==2

def function(signature):
    start=loader.index(signature);opening=loader.index('{',start);depth=0
    for at in range(opening,len(loader)):
        depth += (loader[at]=='{')-(loader[at]=='}')
        if depth==0:return loader[start:at+1]
    raise AssertionError('unterminated production function')

c_prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
struct esp_elfsym { const char* name; const void* sym; };
typedef const struct esp_elfsym esp_elf_symbol_table_t;
#define SYMBOL_TABLES_NO 4
static esp_elf_symbol_table_t* g_symbol_tables[SYMBOL_TABLES_NO];
extern void esp_elf_registered_symbol_used(const void*,const char*,uintptr_t) __attribute__((weak));
'''
registered='\n'.join(function(sig) for sig in [
 'int esp_elf_register_symbol(esp_elf_symbol_table_t *symbol_table)',
 'int esp_elf_unregister_symbol(esp_elf_symbol_table_t *symbol_table)',
 'uintptr_t esp_elf_find_symbol(const char *sym_name)'])
bridge=compat[compat.index('static bool compat_registered'):compat.index('#else',compat.index('static bool compat_registered'))]
cpp=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <StorageGeneration.h>
#include "native/NativeStorageImportPolicy.h"
struct esp_elfsym { const char* name; const void* sym; };
extern "C" int esp_elf_register_symbol(const esp_elfsym*);
extern "C" int esp_elf_unregister_symbol(const esp_elfsym*);
extern "C" uintptr_t esp_elf_find_symbol(const char*);
struct StorageFixture {
 StorageGenerationTracker tracker;
 unsigned begins=0,ends=0;
 void externalStorageBegin(){++begins;tracker.externalBegin();}
 void externalStorageEnd(bool clean){++ends;tracker.externalEnd(clean);}
 void externalStorageUncertain(){tracker.externalUncertain();}
} Storage;
static const esp_elfsym native_hardware_compat_symbols[] = {
'''+''.join(f'{{"{name}",reinterpret_cast<const void*>({i+1}u)}},\n' for i,name in enumerate(names))+r'''
{nullptr,nullptr}};
'''+bridge+r'''
int main(int argc,char** argv){
 assert(argc==2);
 if(!std::strcmp(argv[1],"normal")){
   auto before=Storage.tracker.stamp(true);
   assert(!esp_elf_find_symbol("SD"));assert(before.matches(Storage.tracker.stamp(true)));
   assert(native_hardware_compat_register()==0);
   // Exposing the table is not enough: ordinary scoped-API apps must work.
   assert(before.matches(Storage.tracker.stamp(true)));
   for(const char* name:{"_ZN2fs2FS6existsEPKc","_ZN2fs4File4readEPhj","_ZN2fs4File11isDirectoryEv"}){
     assert(esp_elf_find_symbol(name));assert(before.matches(Storage.tracker.stamp(true)));
   }
   native_hardware_compat_unregister();
   unsigned classified=0;
   for(const auto* symbol=native_hardware_compat_symbols;symbol->name;++symbol){
     if(!nativeRawStorageImport(symbol->name))continue;
     ++classified;
     assert(native_hardware_compat_register()==0);assert(esp_elf_find_symbol(symbol->name));
     assert(!Storage.tracker.stamp(true).quiescent);
     const auto count=Storage.begins;assert(esp_elf_find_symbol(symbol->name));assert(Storage.begins==count);
     // Simulated later relocation failure still closes its uncertainty window.
     assert(!esp_elf_find_symbol("missing_required_import"));
     native_hardware_compat_unregister();assert(!Storage.tracker.stamp(true).quiescent);
     assert(Storage.tracker.needsReconcile()&&Storage.tracker.mountAttempt());
     Storage.tracker.mounted(true);assert(Storage.tracker.stamp(true).quiescent);
   }
   assert(classified==8&&Storage.begins==classified&&Storage.ends==classified);
   assert(!nativeRawStorageImport("_ZN2fs4File5writeEPKhj_spoof"));
 }else{
   assert(native_hardware_compat_register()==0);assert(esp_elf_find_symbol("_ZN2fs2FS4openEPKcS2_b"));
   if(!std::strcmp(argv[1],"retained"))native_hardware_compat_storage_uncertain();
   else assert(esp_elf_unregister_symbol(native_hardware_compat_symbols)==0);
   native_hardware_compat_unregister();assert(!Storage.tracker.stamp(true).quiescent&&!Storage.tracker.mountAttempt());
 }
 puts("Actual registered storage import/lifetime boundary PASS");
}
'''
cpp=cpp.replace('#include <cstdio>','#include <cstdio>\n#include <initializer_list>\nstatic bool retiredStorageImport=false;')
with tempfile.TemporaryDirectory(prefix='u1-storage-import-') as temp:
    directory=Path(temp);c=directory/'lookup.c';c.write_text(c_prefix+registered)
    body=directory/'boundary.cpp';body.write_text(cpp)
    obj=directory/'lookup.o';binary=directory/'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-c',str(c),'-o',str(obj)],check=True)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT/'lib/hal'),'-I'+str(ROOT/'src'),str(body),str(obj),'-o',str(binary)],check=True)
    for mode in ['normal','retained','unregister_failure']:subprocess.run([str(binary),mode],check=True)
