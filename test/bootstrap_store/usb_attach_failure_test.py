from pathlib import Path
import os,re,subprocess
ROOT=Path(__file__).resolve().parents[2]
import tempfile
_temp=tempfile.TemporaryDirectory()
OUT=Path(_temp.name)
sd=re.sub(r'^#include .*\n','',(ROOT/'src/platform/SdPackageBoot.cpp').read_text(),flags=re.M)
loop=(ROOT/'src/platform/X4DiagnosticBoot.cpp').read_text().split('bool x4DiagnosticLoop() {',1)[1].split('\n#endif',1)[0]
loop='bool x4DiagnosticLoop() {'+loop
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#define BOARD_XTEINK_X4_PRO 1
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
static unsigned nowMs=0,mounts=0,validations=0,releases=0,starts=0,polls=0;
static bool serialConnected=false,attached=false,ready=false,showing_home=false;
static std::string fault;
unsigned long millis(){return nowMs;}
void delay(unsigned ms){nowMs+=ms;}
struct SerialState {explicit operator bool()const{return serialConnected;}}logSerial;
struct Surface {bool complete=false;bool lastPresentSucceeded(){return complete;}} surface;
Surface* provider_surface=nullptr;
namespace X4BootDiagnostics {
 enum class Stage {Ready};
 void mark(Stage){assert(showing_home&&provider_surface&&provider_surface->complete);}
 void poll(bool connected){++polls;if(attached)assert(connected);}
}
struct risc_input_navigation_frame_v1{uint32_t pressed;};
enum {RISC_NAV_LEFT=1,RISC_NAV_RIGHT=2,RISC_NAV_CONFIRM=4,RISC_NAV_BACK=8};
risc_input_navigation_frame_v1 nativeNavigationFrame(){assert(ready);return {};}
namespace Board {const char* id(){return "xteink-x4-pro";}}
namespace SdBootReader {
 bool read(const std::string&,size_t,std::vector<uint8_t>&){return true;}
 bool mount(){++mounts;return fault!="mount";}
 bool release(){++releases;return fault!="release";}
}
namespace RuntimeInstalledProviders {
 bool loadBootstrapPackages(const char*,const char*,bool(*)(const std::string&,size_t,std::vector<uint8_t>&)){
  ++validations;return fault!="validation";
 }
 bool finishBootstrapHandoff(){++starts;return fault!="activation";}
}
'''
main=r'''
int main(int argc,char**argv){
 assert(argc==2);fault=argv[1];
 bool boot=loadPlatformSdPackages();assert(boot==(fault=="okay"));
 if(boot){showing_home=true;provider_surface=&surface;surface.complete=true;assert(x4DiagnosticLoop());}
 else {
  for(unsigned i=0;i<1200;++i)assert(!x4DiagnosticLoop());
  assert(nowMs==240000&&!ready&&mounts==1);
  auto before=mounts+validations+releases+starts;
  serialConnected=attached=true;
  for(unsigned i=0;i<50;++i)assert(!x4DiagnosticLoop());
  assert(nowMs==250000&&mounts+validations+releases+starts==before&&!ready);
  // Even an explicit duplicate request remains the already-attempted result.
  assert(!loadPlatformSdPackages()&&mounts+validations+releases+starts==before);
 }
 printf("actual boot orchestration+loop case=%s elapsed_ms=%u mounts=%u validations=%u releases=%u starts=%u ready=%d polls=%u\n",fault.c_str(),nowMs,mounts,validations,releases,starts,ready,polls);
}
'''
cpp=OUT/'usb_attach_probe.cpp';cpp.write_text(prefix+sd+loop+main)
binary=OUT/'usb_attach_probe'
subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined',str(cpp),'-o',str(binary)],check=True)
for case in ['okay','mount','validation','release','activation']:
 subprocess.run([str(binary),case],check=True,timeout=10,env=os.environ)
