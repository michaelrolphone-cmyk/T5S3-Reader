from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
files={
'esp_heap_caps.h':r'''
#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline void* heap_caps_malloc(size_t n,unsigned){return malloc(n);}
inline void* heap_caps_calloc(size_t n,size_t s,unsigned){return calloc(n,s);}
inline void* heap_caps_realloc(void* p,size_t n,unsigned){return realloc(p,n);}
inline void heap_caps_free(void* p){free(p);}
inline unsigned heap_caps_get_free_size(unsigned){return 0;}
inline unsigned heap_caps_get_largest_free_block(unsigned){return 0;}
''',
'Logging.h':'#define LOG_INF(...) ((void)0)\n#define LOG_ERR(...) ((void)0)\n',
'freertos/FreeRTOS.h':'''#pragma once
#include <cstdint>
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
using TaskHandle_t=void*;
''',
'freertos/semphr.h':'''#pragma once
using SemaphoreHandle_t=void*;
inline void* xSemaphoreCreateMutex(){return (void*)1;}
inline int xSemaphoreTake(void*,unsigned){return 1;}
inline int xSemaphoreGive(void*){return 1;}
''',
'freertos/task.h':'''#pragma once
inline void vTaskDelay(unsigned){}
static void* callingTask=(void*)1;
inline void* xTaskGetCurrentTaskHandle(){return callingTask;}
'''
}
main=r'''
#include <cassert>
#include <cstdio>
#include "native/NativeAppMemory.cpp"
int main(){
 assert(!native_app_memory_symbol("malloc"));
 for(int i=0;i<100;++i){
  assert(native_app_memory_begin());
  assert(!native_app_memory_begin());
  native_app_memory_relocation(true);
  auto alloc=(void*(*)(size_t))native_app_memory_symbol("malloc");
  auto drop=(void(*)(void*))native_app_memory_symbol("free");
  assert(alloc && drop);
  callingTask=(void*)2; assert(!native_app_memory_symbol("malloc")); callingTask=(void*)1;
  native_app_memory_relocation(false);
  assert(!native_app_memory_symbol("malloc")); // nested driver/firmware mapping stays independent
  assert(alloc(64)); assert(native_app_psram_alloc(1024));
  auto p=alloc(3); drop(p); assert(ledger.count()==2);
  auto foreign=malloc(16); drop(foreign); assert(ledger.count()==2);
  native_app_memory_end(); assert(!entries && ledger.count()==0);
  native_app_memory_end(); assert(!alloc(32));
 }
 puts("actual app memory bridge: scoped relocation, repeated reclamation, foreign-pointer free PASS");
}
'''
resolver=(ROOT/'lib/elf_loader/src/esp_elf_symbol.c').read_text()
assert 'if (!privileged_scope) {\n        uintptr_t app_memory = native_app_memory_symbol' in resolver
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp)
 for name,text in files.items():
  f=p/name; f.parent.mkdir(parents=True,exist_ok=True); f.write_text(text)
 (p/'main.cpp').write_text(main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(ROOT/'src'),str(p/'main.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
