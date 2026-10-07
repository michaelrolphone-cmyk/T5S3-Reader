#!/usr/bin/env python3
"""Execute the production boot orchestration with failed ownership barriers."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=re.sub(r'^#include .*\n','',(root/'src/platform/SdPackageBoot.cpp').read_text(),flags=re.M)
fixture=r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#define BOARD_XTEINK_X4_PRO 1
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
static std::string fault, events;
static bool bootOwner=false, runtimeAllowed=false;
namespace Board {const char* id(){return "test-board";}}
namespace SdBootReader {
 bool read(const std::string&,size_t,std::vector<uint8_t>&){return true;}
 bool mount(){events+="M";bootOwner=true;return fault!="mount";}
 bool release(){assert(bootOwner);events+="R";if(fault=="release")return false;bootOwner=false;return true;}
}
namespace RuntimeInstalledProviders {
 bool loadBootstrapPackages(const char* root,const char* board,
  bool(*reader)(const std::string&,size_t,std::vector<uint8_t>&)) {
  assert(bootOwner&&!runtimeAllowed&&reader==SdBootReader::read);
  assert(std::string(root)=="/sdboot"&&std::string(board)=="test-board");
  events+="V";return fault!="validation";
 }
 bool finishBootstrapHandoff(){assert(!bootOwner);events+="A";runtimeAllowed=true;return true;}
}
'''
main=r'''
int main(int argc,char**argv){
 assert(argc==2);fault=argv[1];
 bool ready=loadPlatformSdPackages();assert(ready==(fault=="okay"));
 assert(runtimeAllowed==ready);
 assert(events==(fault=="mount"?"M":fault=="okay"?"MVRA":"MVR"));
 auto before=events;assert(loadPlatformSdPackages()==ready&&events==before);
 if(fault=="release"||fault=="mount")assert(bootOwner&&!runtimeAllowed);
}
'''
with tempfile.TemporaryDirectory() as temp:
 path=Path(temp)/'test.cpp';binary=Path(temp)/'test'
 path.write_text(fixture+source+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(path),'-o',str(binary)],check=True)
 for case in ('okay','mount','validation','release'):
  subprocess.run([str(binary),case],check=True)
# Production board paths may not retain an internal-flash driver route.
for name in ('PlatformStorage.cpp','X4DiagnosticBoot.cpp'):
 code=(root/'src/platform'/name).read_text()
 assert 'loadPlatformSdPackages()' in code
 assert '/bootfs' not in code and 'mountFlashModuleStore' not in code
assert not (root/'src/platform/FlashModuleStore.cpp').exists()
print('SD boot ownership: validation before release, no activation after failed barrier, no retry/fallback PASS')
