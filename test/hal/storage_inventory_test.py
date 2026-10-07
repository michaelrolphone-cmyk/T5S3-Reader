#!/usr/bin/env python3
"""Compile actual retained-inventory functions against real HalStorage and fault media."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/native/NativePackageManagerBridge.cpp").read_text()
globals_ = source[source.index("constexpr uint32_t kMaxInstalledPackages"):source.index("using Mutation =")]
functions = source[source.index("bool refreshInstalled()"):source.index("bool archiveMetadata(")]
prefix = r'''
#define HAL_STORAGE_IMPL
#include <HalStorage.h>
#include <SdFat.h>
#include <T5PackageManagerApi.h>
#include "runtime/packages/PackageOrdinarySdAdapter.h"
#include "runtime/packages/PackageOrdinaryStage.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace RuntimePackages;
constexpr PackageRuntimePolicy kPolicy{"xtensa-esp32s3",2,8u*1024u*1024u,16u*1024u*1024u};
int caller = 4;
int callerKind(){return caller;}
uint32_t availableCapability(const char*){return 0;}
bool mutate = false;
namespace RuntimePackages {
bool inspectInstalledOrdinarySdDirectory(const char* path,const PackageRuntimePolicy&,uint32_t (*)(const char*),Identity& out){
 if(mutate){mutate=false;assert(Storage.writeFile("/interleaved","write"));}
 return makeIdentity(Kind::Application,std::strrchr(path,'/')+1,"1.0.0","app.elf",false,&out);
}
}
'''
main = r'''
int main(){
 assert(Storage.begin());assert(Storage.mkdir("/Apps/demo"));
 assert(Storage.writeFile("/Apps/demo/.package.json","fixture"));
 t5_installed_package_t row{};
 assert(refreshInstalled()&&installedCount()==1&&installedGet(0,&row));
 assert(row.valid_installation&&!std::strcmp(row.id,"demo"));
 assert(Storage.writeFile("/unrelated","write"));assert(!installedCount()&&!installedGet(0,&row));
 assert(refreshInstalled());Storage.externalStorageBegin();
 assert(!installedCount()&&!refreshInstalled());Storage.externalStorageEnd(true);
 assert(refreshInstalled());auto writer=Storage.open("/open",O_WRONLY|O_CREAT);
 assert(!installedCount()&&!refreshInstalled());assert(writer.close());
 assert(refreshInstalled());assert(Storage.begin());assert(!installedCount());
 mutate=true;assert(!refreshInstalled()&&!installedCount());
 FakeSd::failDirectory="/apps";assert(!refreshInstalled()&&!installedCount());FakeSd::failDirectory.clear();
 FakeSd::failOpen="/providers";FakeSd::mediaError=1;
 assert(!refreshInstalled()&&!installedCount());FakeSd::failOpen.clear();FakeSd::mediaError=0;
 assert(refreshInstalled());caller=0;assert(!installedCount()&&!installedGet(0,&row));caller=4;
 for(unsigned n=0;n<128;++n){auto p="/Apps/item"+std::to_string(n);assert(Storage.mkdir(p.c_str()));assert(Storage.writeFile((p+"/.package.json").c_str(),"fixture"));}
 assert(!refreshInstalled()&&!installedCount());
 std::cout<<"Production Package Manager inventory: all-or-nothing, mutation, writer/raw windows, remount, interleaving, I/O errors and overflow PASS\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/"inventory.cpp").write_text(prefix+globals_+functions+main)
 subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-overloaded-virtual","-I"+str(ROOT/"test/hal/storage_stubs"),"-I"+str(ROOT/"lib/hal"),"-I"+str(ROOT/"lib/NativeApps/include"),"-I"+str(ROOT/"src"),str(ROOT/"lib/hal/HalStorage.cpp"),str(p/"inventory.cpp"),"-o",str(p/"test")],check=True)
 subprocess.run([str(p/"test")],check=True)
