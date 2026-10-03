#!/usr/bin/env python3
"""Execute production platform composition with missing-provider graph fixtures."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=re.sub(r'^#include .*\n','',(root/'src/platform/PlatformStorage.cpp').read_text(),flags=re.M)
fixture=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <RiscStorageVolumeV1.h>
#include <RiscFrontlightV1.h>
#define BOARD_T5S3_PRO 1
#define LOG_ERR(...) ((void)0)
static const char* scenario;
static unsigned mounts,loads,acquires,binds,fallbacks,attaches;
static bool is(const char* value){return !std::strcmp(scenario,value);}
static risc_storage_volume_api_v1 volume;
static risc_frontlight_api_v1 light;
namespace Board {
const char* id(){return "t5s3-pro";}
bool attachFrontlight(const risc_frontlight_api_v1* api){assert(api==&light);++attaches;return true;}
}
struct StorageStub {
 bool begin(){++fallbacks;return true;}
 bool ready(){return !is("media");}
 bool bindVolume(const risc_storage_volume_api_v1* api){assert(api==&volume);++binds;return ready();}
} Storage;
namespace RuntimeInstalledProviders {
struct Grant{unsigned slot;unsigned generation;};
struct Lease{Grant grant{};const void* interface=nullptr;};
bool loadBootstrapPackages(const char* path,const char* board){assert(!std::strcmp(path,"/bootfs")&&!std::strcmp(board,"t5s3-pro"));++loads;return !is("packages");}
bool acquireCapability(const char* cap,unsigned version,Lease* out){
 assert(version==1);++acquires;
 if(!std::strcmp(cap,"storage.volume")){
  if(is("storage"))return false;
  *out={{1,12},&volume};return true;
 }
 assert(!std::strcmp(cap,"display.frontlight"));
 if(is("light"))return false;
 *out={{2,13},&light};return true;
}
bool drainExcept(const Lease* leases,size_t count){
 assert(count==(is("light")?1u:2u));assert(leases[0].grant.slot==1&&leases[0].grant.generation==12);
 if(count==2)assert(leases[1].grant.slot==2&&leases[1].grant.generation==13);
 return true;
}
bool shutdown(){return true;}
}
bool mountFlashModuleStore(){++mounts;return !is("store");}
'''
main=r'''
int main(int argc,char**argv){
 assert(argc==2);scenario=argv[1];
 bool okay=beginPlatformStorage();assert(okay==is("okay"));
 unsigned before=acquires;assert(beginPlatformStorage()==okay);assert(acquires==before);
 assert(mounts==1 && !fallbacks);
 if(is("store"))assert(!loads&&!acquires&&!binds);
 if(is("packages"))assert(loads==1&&!acquires&&!binds);
 if(is("storage"))assert(acquires==1&&!binds&&!attaches);
 if(is("light"))assert(acquires==2&&binds==1&&!attaches);
 if(is("okay")||is("media"))assert(acquires==2&&binds==1&&attaches==1);
 assert(drainPlatformProvidersForSleep());
 puts("Platform storage: missing provider fails closed; exact persistent leases retained PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 path=Path(tmp)/'test.cpp';binary=Path(tmp)/'test';path.write_text(fixture+source+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(root/'sdk/driver'),str(path),'-o',str(binary)],check=True)
 for scenario in ('store','packages','storage','light','media','okay'):subprocess.run([str(binary),scenario],check=True)
