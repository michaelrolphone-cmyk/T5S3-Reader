#!/usr/bin/env python3
"""Host operation counts through actual default app, launcher and memory bridge."""
import argparse,ast,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
R=a.checkout.resolve();D=a.output.resolve();D.mkdir(parents=True,exist_ok=True)
node=next(n for n in ast.parse((R/'test/native_apps/app_memory_bridge_test.py').read_text()).body if isinstance(n,ast.Assign) and n.targets[0].id=='files')
files=ast.literal_eval(node.value)
files['freertos/task.h']='''#pragma once
extern "C" void count_wait(unsigned);
inline void vTaskDelay(unsigned n){count_wait(n);}
static void* callingTask=(void*)1;
inline void* xTaskGetCurrentTaskHandle(){return callingTask;}
'''
files['esp_log.h']='#pragma once\n#include <stdio.h>\n#define ESP_LOGE(tag,...) do {(void)(tag);if(0)fprintf(stderr,__VA_ARGS__);}while(0)\n#define ESP_LOGI(tag,...) do {(void)(tag);if(0)fprintf(stderr,__VA_ARGS__);}while(0)\n'
for name,text in files.items():
 f=D/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(text)
old=(R/'test/native_apps/launcher_test.c').read_text()
headers=old[:old.index('extern int test_compat_storage_uncertain')]
apis=old[old.index('const t5_app_api_v1 *t5_app_get_api'):old.index('// Memory lifetime starts')]
fixture= r'''
extern void app_main(void);
extern unsigned count_live(void), count_entries(void);
extern void reset_work(void), emit_work(const char*), exercise_live_case(unsigned,unsigned);
static unsigned waits, ticks, close_waits, opens, closes, pumps;
static int handle;
static bool fail_load,fail_symbol;
static const char*error;
void count_wait(unsigned n){++waits;ticks+=n;}
unsigned get_waits(void){return waits;}
unsigned get_ticks(void){return ticks;}
void clear_waits(void){waits=ticks=0;}
const char*dlerror(void){const char*r=error;error=NULL;return r;}
void*dlopen(const char*p,int f){assert(strstr(p,"default.elf")&&f==RTLD_NOW);++opens;if(fail_load){error="fixture read failure";return NULL;}return &handle;}
void*dlsym(void*h,const char*n){assert(h==&handle);if(!strcmp(n,"app_main")&&!fail_symbol)return (void*)app_main;error="fixture symbol absent";return NULL;}
int dlclose(void*h){assert(h==&handle&&!count_entries()&&!count_live());++closes;close_waits=waits;return 0;}
esp_err_t native_app_register_sd_vfs(void){return ESP_OK;}
int esp_elf_register_symbol(const struct esp_elfsym*s){assert(s);return 0;}
esp_err_t native_hardware_takeover_begin(uint32_t m){(void)m;assert(false);return ESP_FAIL;}
esp_err_t native_hardware_takeover_end(uint32_t m){(void)m;assert(false);return ESP_FAIL;}
bool native_hardware_display_is_borrowed(void){return false;}
static uint32_t pump(void){++pumps;assert(!strcmp(native_app_current_path(),"/sd/Apps/default/default.elf"));assert(count_live()==0);return T5_READER_ENTRY_HANDOFF;}
static const t5_reader_entry_api_v1 reader={T5_READER_ENTRY_ABI_VERSION,sizeof(reader),pump};
const t5_reader_entry_api_v1*t5_reader_entry_get_api(uint32_t v){return v==T5_READER_ENTRY_ABI_VERSION?&reader:NULL;}
int main(void){
 clear_waits();assert(launch_elf_reader_entry("/sd/Apps/default/default.elf")==ESP_OK);
 assert(pumps==1&&opens==1&&closes==1&&close_waits==64&&waits==64&&ticks==64);emit_work("one_paperspace_handoff_zero_live");
 clear_waits();for(unsigned i=0;i<20;++i)assert(launch_elf_reader_entry("/sd/Apps/default/default.elf")==ESP_OK);
 assert(waits==1280&&ticks==1280);emit_work("20_paperspace_handoffs_zero_live");
 clear_waits();fail_load=true;assert(launch_elf_reader_entry("/sd/Apps/default/default.elf")==ESP_FAIL);assert(waits==64);emit_work("dlopen_failure_zero_live");fail_load=false;
 clear_waits();fail_symbol=true;assert(launch_elf_reader_entry("/sd/Apps/default/default.elf")==ESP_ERR_NOT_FOUND);assert(waits==64);emit_work("missing_entry_zero_live");fail_symbol=false;
 for(unsigned n=0;n<=3;++n)exercise_live_case(n,n);
 exercise_live_case(4096,4095);exercise_live_case(4096,0);
 return 0;
}
'''
(D/'fixture.c').write_text(headers+apis+fixture)
bridge=r'''
#include <cassert>
#include <cstdio>
#include <vector>
#include "native/NativeAppMemory.cpp"
extern "C" unsigned get_waits(void),get_ticks(void);
extern "C" void clear_waits(void);
extern "C" unsigned count_live(void){return ledger.count();}
extern "C" unsigned count_entries(void){return entries!=nullptr;}
extern "C" void emit_work(const char*n){printf("{\"case\":\"%s\",\"wait_requests\":%u,\"requested_ticks\":%u,\"remaining_live\":%u}\n",n,get_waits(),get_ticks(),count_live());}
extern "C" void exercise_live_case(unsigned allocated,unsigned freed){
 assert(freed<=allocated&&allocated<=4096);assert(native_app_memory_begin());
 std::vector<void*> pointers;
 for(unsigned i=0;i<allocated;++i){auto p=appMalloc(1);assert(p);pointers.push_back(p);}
 for(unsigned i=0;i<freed;++i)native_app_memory_free(pointers[i]);
 assert(ledger.count()==allocated-freed);clear_waits();native_app_memory_end();
 assert(get_waits()==64&&get_ticks()==64&&!entries&&!ledger.count());
 native_app_memory_end();assert(get_waits()==64);
 char n[100];std::snprintf(n,sizeof(n),"allocated_%u_freed_%u_live_%u",allocated,freed,allocated-freed);emit_work(n);
}
'''
(D/'bridge.cpp').write_text(bridge)
flags=['-Ddlopen=fixture_dlopen','-Ddlsym=fixture_dlsym','-Ddlerror=fixture_dlerror','-Ddlclose=fixture_dlclose','-Wall','-Wextra','-Werror','-fno-omit-frame-pointer','-I'+str(D),'-I'+str(R/'test/native_apps/stubs'),'-I'+str(R/'lib/NativeApps/include'),'-I'+str(R/'src')]
if a.sanitize:flags+=['-fsanitize=address,undefined']
sources=[R/'lib/NativeApps/src/NativeAppLauncher.c',R/'Apps/default.c',R/'test/native_apps/programmer_api_stub.c',R/'test/native_apps/compat_registration_stub.c',D/'fixture.c']
objects=[]
for i,s in enumerate(sources):
 o=D/f'{i}.o';subprocess.run(['cc','-std=c11',*flags,'-c',str(s),'-o',str(o)],check=True);objects.append(str(o))
subprocess.run(['c++','-std=c++17',*flags,str(D/'bridge.cpp'),*objects,'-o',str(D/'probe')],check=True)
subprocess.run([str(D/'probe')],env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'},check=True,timeout=30)
paths=['Apps/default.c','Apps/default.json','lib/NativeApps/src/NativeAppLauncher.c','src/native/NativeAppMemory.cpp','src/runtime/resources/AppAllocationLedger.h','src/native/NativeReaderEntry.cpp','src/activities/ActivityManager.cpp','test/native_apps/app_memory_bridge_test.py','test/native_apps/launcher_test.c','test/native_apps/programmer_api_stub.c','test/native_apps/compat_registration_stub.c']
(D/'source-hashes.json').write_text(json.dumps({x:hashlib.sha256((R/x).read_bytes()).hexdigest() for x in paths},indent=2)+'\n')
