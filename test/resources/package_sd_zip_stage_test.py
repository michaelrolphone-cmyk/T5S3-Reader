#!/usr/bin/env python3
"""Compile the actual SD ZIP stage with the existing faultable storage fake."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source_path = 'src/runtime/packages/PackageOrdinarySdZipAdapter.cpp'
source = (subprocess.check_output(['git','show',os.environ['U1_ZIP_STAGE_BASELINE']+':'+source_path],cwd=ROOT,text=True)
          if os.environ.get('U1_ZIP_STAGE_BASELINE') else (ROOT/source_path).read_text())
read_file = source[source.index('bool readFile('):source.index('class Archive {')]
stage = source[source.index('bool inventory('):source.index('struct Ops {')]
fixture = r'''
#include "runtime/packages/PackageOrdinarySdTree.h"
#include "runtime/packages/PackageOrdinaryTransaction.h"
#include "runtime/packages/PackageVerificationReceiptSd.h"
#include <cassert>
#include <cstdio>
using namespace RuntimePackages;
constexpr size_t kManifestCapacity=4096;
namespace RuntimePackages { bool newPackageReceiptGeneration(uint8_t (&out)[16]) {
 static uint8_t next=1;std::memset(out,next++,sizeof(out));return true;
} }
'''+read_file+stage+r'''
int main(){
 OrdinaryPackagePlan plan{};plan.schemaVersion=3;plan.entryCount=1;
 assert(makeResourceIdentity(Kind::Service,"guide","1.0.0",&plan.identity));
 std::strcpy(plan.entries[0].name,"help/nested/guide.txt");
 OrdinaryTransactionPaths paths{};assert(ordinaryTransactionPaths(Kind::Service,"guide",paths));
 for(int failure=0;failure<7;++failure){
  assert(!treeSd.handles);treeSd={};Stage stage;assert(stage.begin(plan));
  if(failure==1)treeSd.createFailure=std::string(paths.stage)+"/help/nested";
  const bool opened=stage.beginEntry("help/nested/guide.txt",3);
  if(failure==1){assert(!opened);assert(stage.discard());continue;}
  assert(opened);assert(stage.append(reinterpret_cast<const uint8_t*>("abc"),3));assert(stage.endEntry());
  uint8_t bytes[3]{};assert(stage.readEntry("help/nested/guide.txt",0,bytes,3));assert(!memcmp(bytes,"abc",3));
  const std::string receipt=std::string(paths.stage)+"/.package.receipt";
  if(failure==4)treeSd.writeFailure=receipt;
  if(failure==5)treeSd.readFailure=receipt;
  if(failure==6)treeSd.closeFailure=receipt;
  if(failure>=4){
    assert(!stage.writeManifest(reinterpret_cast<const uint8_t*>("{}"),2));
    if(failure==6){assert(!stage.discard());assert(treeSd.nodes.count(paths.stage));}
    else assert(stage.discard());
    continue;
  }
  assert(stage.writeManifest(reinterpret_cast<const uint8_t*>("{}"),2));assert(stage.seal());
  if(failure==2){
   const std::string extra=std::string(paths.stage)+"/help/owner.txt";treeSd.nodes[extra]=false;
   const auto before=treeSd.nodes;assert(!stage.seal());assert(!stage.discard());assert(treeSd.nodes==before);
   treeSd.nodes.erase(extra);
  }
  if(failure==3){
   treeSd.readFailure=std::string(paths.stage)+"/help";const auto before=treeSd.nodes;
   assert(!stage.seal());assert(!stage.discard());assert(treeSd.nodes==before);treeSd.readFailure.clear();
  }
  assert(stage.discard());assert(!treeSd.nodes.count(paths.stage));assert(!treeSd.handles);
 }
 puts("Actual SD ZIP stage: nested creation/readback/seal, failed parents, unknown data and iterator failure cleanup PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='u1-sd-zip-stage-') as d:
    path=Path(d);(path/'test.cpp').write_text(fixture)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer','-I'+str(ROOT/'src'),'-I'+str(ROOT/'test/resources/tree_stubs'),'-I'+str(ROOT/'test/resources/cdc_sd_stubs'),
                    str(path/'test.cpp'),'-lcrypto','-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True,env=os.environ.copy())
