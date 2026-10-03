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
#include <RiscGpioExpanderV1.h>
#include <RiscDisplayPowerV1.h>
#include <T5DisplayProviderV1.h>
#include <T5VideoApi.h>
bool beginPlatformBoardProviders();
#define BOARD_T5S3_PRO 1
#define LOG_ERR(...) ((void)0)
static const char* scenario;
static unsigned mounts,loads,acquires,binds,fallbacks,attaches;
static bool is(const char* value){return !std::strcmp(scenario,value);}
static risc_storage_volume_api_v1 volume;
static risc_frontlight_api_v1 light;
static risc_gpio_expander_api_v1 expander;
static risc_display_power_api_v1 power;
static t5_display_quality_api_v1 quality={1,sizeof(quality),[](bool){return true;},
 [](const uint8_t*,size_t,uint16_t,uint16_t,uint16_t,uint16_t,uint8_t){return true;},
 [](uint32_t){return true;},[](bool){return true;},[](bool){return true;},[](){return true;}};
static t5_video_api_v1 fast={};
static t5_display_provider_api_v1 displayProvider={};
static unsigned held;
namespace Board {
const char* id(){return "t5s3-pro";}
bool attachExpander(const risc_gpio_expander_api_v1* api){assert(api==&expander);return !is("pins");}
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
 if(!std::strcmp(cap,"display.output")){
  ++held;*out={{5,16},&displayProvider};return true;
 }
 if(!std::strcmp(cap,"storage.volume")){
  if(is("storage"))return false;
  ++held;*out={{4,15},&volume};return true;
 }
 if(!std::strcmp(cap,"gpio.expander")){
  if(is("expander"))return false;
  ++held;*out={{1,12},&expander};return true;
 }
 if(!std::strcmp(cap,"display.power")){
  if(is("power"))return false;
  ++held;*out={{3,14},&power};return true;
 }
 assert(!std::strcmp(cap,"display.frontlight"));
 if(is("light"))return false;
 ++held;*out={{2,13},&light};return true;
}
bool drainExcept(const Lease* leases,size_t count){
 assert(count==held);
 for(size_t i=0;i<count;++i){
  const unsigned expected=is("timer")&&i==3?5:i+1;
  assert(leases[i].grant.slot==expected && leases[i].grant.generation==expected+11);
 }
 return true;
}
bool shutdown(){return true;}
}
bool mountFlashModuleStore(){++mounts;return !is("store");}
'''
main=r'''
int main(int argc,char**argv){
 assert(argc==2);scenario=argv[1];
 // Early clock/board path mounts only immutable packages, not SD.
 bool board=beginPlatformBoardProviders();
 assert(board==(is("okay")||is("media")||is("storage")||is("timer")));
 assert(!binds);
 if(is("timer")){
  assert(acquires==3);
  displayProvider.base={};displayProvider.base.api_version=1;displayProvider.base.struct_size=sizeof(displayProvider);
  displayProvider.extension_tag=T5_DISPLAY_EXTENSION_TAG;displayProvider.extension_version=1;
  displayProvider.quality=&quality;displayProvider.fast=&fast;fast.api_version=1;fast.struct_size=sizeof(fast);
  assert(platformDisplayProvider()==&displayProvider && acquires==4 && !binds);
  assert(drainPlatformProvidersForSleep());return 0;
 }
 bool okay=beginPlatformStorage();assert(okay==is("okay"));
 unsigned before=acquires;assert(beginPlatformStorage()==okay);assert(acquires==before);
 assert(beginPlatformBoardProviders()==board && acquires==before);
 assert(mounts==1 && !fallbacks);
 if(is("store"))assert(!loads&&!acquires&&!binds);
 if(is("packages"))assert(loads==1&&!acquires&&!binds);
 if(is("expander"))assert(acquires==1&&!binds&&!attaches);
 if(is("pins"))assert(acquires==1&&held==1&&!binds&&!attaches);
 if(is("light"))assert(acquires==2&&!binds&&!attaches);
 if(is("power"))assert(acquires==3&&!binds&&attaches==1);
 if(is("storage"))assert(acquires==4&&!binds&&attaches==1);
 if(is("okay")||is("media"))assert(acquires==4&&binds==1&&attaches==1);
 assert(drainPlatformProvidersForSleep());
 puts("Platform storage: missing provider fails closed; exact persistent leases retained PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 path=Path(tmp)/'test.cpp';binary=Path(tmp)/'test';path.write_text(fixture+source+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(root/'sdk/driver'),'-I'+str(root/'lib/NativeApps/include'),str(path),'-o',str(binary)],check=True)
 for scenario in ('store','packages','expander','pins','power','storage','light','media','okay','timer'):subprocess.run([str(binary),scenario],check=True)
