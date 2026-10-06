#!/usr/bin/env python3
"""Compile real SD provider/FatFs/HAL and package readers; compare exact source trees.

Place beside direct_read_semantics_test.cpp under test/storage_volume.
Default source root is the repository containing this runner. Optional baseline
is independently compiled, without rewriting its production source. Transport
models and host shims are not hardware timing/physical qualification.
"""
from pathlib import Path
import argparse,subprocess,sys,os,tempfile,json,hashlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-root',type=Path,default=Path(__file__).resolve().parents[2])
p.add_argument('--baseline-root',type=Path)
p.add_argument('--output-dir',type=Path)
p.add_argument('--spi',action='store_true')
p.add_argument('--no-sanitize',action='store_true')
p.add_argument('--require-direct-budget',action='store_true')
p.add_argument('--baseline-must-fail-budget',action='store_true')
a=p.parse_args()
assert not a.baseline_must_fail_budget or a.baseline_root
scratch=None
if a.output_dir: out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
else:scratch=tempfile.TemporaryDirectory(prefix='direct-read-semantics-');out=Path(scratch.name)
fixture=Path(__file__).with_name('direct_read_semantics_test.cpp')
assert fixture.is_file()
variants=[('candidate',a.source_root.resolve())]
if a.baseline_root:variants.insert(0,('baseline',a.baseline_root.resolve()))
san=[] if a.no_sanitize else ['-fsanitize=address,undefined']
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
results=[]
for name,root in variants:
 build=out/name;build.mkdir(exist_ok=True);inc=build/'include';(inc/'freertos').mkdir(parents=True,exist_ok=True)
 sys.path.insert(0,str(root/'test/storage_volume'))
 from mmio_boundary import header
 (inc/'x4pro_mmio.h').write_text(header(root))
 # Only operating-system/app registration and digest shims are substituted.
 # Filesystem, provider, generic HAL, metadata readers and admission stay real.
 (inc/'esp_err.h').write_text('#pragma once\nusing esp_err_t=int; constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_ERR_NO_MEM=2;\n')
 (inc/'esp_task_wdt.h').write_text('#pragma once\ninline void esp_task_wdt_reset() {}\n')
 (inc/'esp_timer.h').write_text('#pragma once\nextern "C" uint64_t card_time; inline uint64_t esp_timer_get_time(){return card_time*1000;}\n')
 (inc/'freertos/FreeRTOS.h').write_text('#pragma once\n#include <cstdint>\n')
 (inc/'freertos/task.h').write_text('#pragma once\n#include <Arduino.h>\ninline void vTaskDelay(unsigned n){delay(n);}\n')
 (inc/'freertos/semphr.h').write_text('''#pragma once
#include <cassert>
struct StaticSemaphore_t {bool held=false;};
using SemaphoreHandle_t=StaticSemaphore_t*;
constexpr unsigned portMAX_DELAY=~0u;
namespace FakeLock {inline unsigned held=0;inline bool deny=false;}
inline SemaphoreHandle_t xSemaphoreCreateMutex(){static StaticSemaphore_t storage;return &storage;}
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* s){return s;}
inline int xSemaphoreTake(SemaphoreHandle_t s,unsigned){if(FakeLock::deny)return 0;assert(s&&!s->held);s->held=true;++FakeLock::held;return 1;}
inline int xSemaphoreGive(SemaphoreHandle_t s){assert(s&&s->held);s->held=false;--FakeLock::held;return 1;}
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
''')
 (inc/'esp_vfs.h').write_text('''#pragma once
#include <esp_err.h>
#include <sys/types.h>
#include <sys/stat.h>
constexpr int ESP_VFS_FLAG_DEFAULT=0;
struct esp_vfs_t {int flags; int(*open)(const char*,int,int); ssize_t(*read)(int,void*,size_t);off_t(*lseek)(int,off_t,int);int(*close)(int);int(*fstat)(int,struct stat*);};
inline esp_vfs_t captured{};
inline esp_err_t esp_vfs_register(const char*,const esp_vfs_t* v,void*){captured=*v;return ESP_OK;}
''')
 (inc/'NativeAppLauncher.h').write_text('#pragma once\n#include <esp_err.h>\nextern "C" int native_app_register_sd_vfs();\nconst char* native_app_current_path();\n')
 card=(root/'test/storage_volume/inventory_test.cpp').read_text()
 (inc/'card.inc').write_text(card[:card.index('static const risc_storage_volume_api_v1* inspectorApi;')])
 paths=['src/native/AppPackageInstaller.cpp','src/native/ManagedAppAdmission.cpp','src/runtime/packages/PackageExecutableAdmission.cpp','lib/NativeApps/src/SdVfs.cpp','src/runtime/packages/PackageVerificationReceiptSd.h','lib/hal/HalStorageVolume.cpp']
 hashes={path:hashlib.sha256((root/path).read_bytes()).hexdigest() for path in paths}
 (build/'source-hashes.json').write_text(json.dumps(hashes,indent=2))
 # Include whole unmodified translation units so private-function probes execute
 # the actual function bodies. --gc-sections drops unrelated entry points.
 (build/'app_private.cpp').write_text('#include "'+str(root/paths[0])+'"\nbool testRegular(const char* p){return RuntimePackages::existingRegularFile(p);}\nbool testBytes(const char* p,uint64_t n,const char* h,bool full){return RuntimePackages::verifyBytes(p,n,h,full);}\n')
 (build/'managed_private.cpp').write_text('#include "'+str(root/paths[1])+'"\nbool testMetadata(const std::string& p,size_t m,std::string& out,bool& u){return RuntimePackages::readMetadata(p,m,out,u);}\n')
 common=['-O1','-g','-Wall','-Wextra','-Werror',*san,'-pthread','-I'+str(inc)]
 objects=[]
 for path in ['Drivers/t5s3_sd/driver.c' if a.spi else 'Drivers/x4pro_sd/driver.c','Drivers/storage_fatfs/fatfs/ff.c','Drivers/storage_fatfs/fatfs/ffunicode.c','test/storage_volume/os_cpu_fake.c']:
  obj=build/(Path(path).name+'.o');objects.append(str(obj))
  subprocess.run(['cc','-std=c11',*common,'-Wno-overflow','-D_XOPEN_SOURCE=700','-Isdk/driver','-IDrivers/x4pro_board','-Itest/storage_volume/fake','-c',path,'-o',str(obj)],cwd=root,check=True)
 cpp=['c++','-std=c++17',*common,'-DBOARD_T5S3_PRO' if a.spi else '-DBOARD_XTEINK_X4_PRO','-DARDUINO_ARCH_ESP32','-Wno-overloaded-virtual','-Wno-unused-function','-Itest/storage_volume','-Itest/storage_volume/inventory_stubs','-Itest/storage_volume/stubs','-include','Arduino.h','-Ilib/hal','-Isdk/driver','-Isrc','-Isrc/native','-Isrc/runtime/packages','-Ilib/NativeApps/include','-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
 if a.spi:cpp.append('-DTEST_SPI_TRANSPORT')
 binary=build/'semantics'
 subprocess.run([*cpp,str(fixture),str(build/'app_private.cpp'),str(build/'managed_private.cpp'),paths[2],paths[3],paths[5],*objects,'-lcrypto','-o',str(binary)],cwd=root,check=True)
 scenarios=['normal','reject','open-error','exhaustion','close-bytes','read-error','close-file','close-metadata','close-admission','close-vfs','stale','wrongtype-close','receipt','stat-false','media-error','coherency','vfs-reject-close']
 local_env=dict(env)
 if name=='candidate' and a.require_direct_budget:local_env['DIRECT_READ_REQUIRE_ZERO_STATS']='1'
 for scenario in scenarios:
  run=subprocess.run([str(binary),scenario],env=local_env,text=True,capture_output=True,timeout=120)
  (build/(scenario+'.log')).write_text(run.stdout+run.stderr)
  results.append({'variant':name,'scenario':scenario,'exit_code':run.returncode})
  print(name,scenario,run.returncode,flush=True)
  for line in (run.stdout+run.stderr).splitlines():
   if line.startswith(('RESULT','CONTROL','PASS')) or run.returncode:print(line,flush=True)
  if run.returncode:raise RuntimeError(f'{name} {scenario} failed: {build/scenario}.log')
 if name=='baseline' and a.baseline_must_fail_budget:
  negative=subprocess.run([str(binary),'normal'],env=dict(env,DIRECT_READ_REQUIRE_ZERO_STATS='1'),text=True,capture_output=True,timeout=120)
  (build/'budget-negative.log').write_text(negative.stdout+negative.stderr)
  assert negative.returncode!=0 and 'direct-read stat budget' in negative.stderr
  print('baseline independently fails direct-read stat budget PASS',flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2))
print('All requested source/transport cases passed. Artifacts:',out,flush=True)
if scratch:scratch.cleanup()
