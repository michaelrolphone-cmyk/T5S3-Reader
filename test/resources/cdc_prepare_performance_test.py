#!/usr/bin/env python3
"""Measure actual warm prepare() paths against filesystem-backed CDC fixtures.

The pre-U1 source is pinned; --baseline compiles the pre-cache CDC implementation
from the specified commit. Timing is host filesystem time, never device latency.
"""
from pathlib import Path
import argparse,json,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser();ap.add_argument('--baseline');ap.add_argument('--current-only',action='store_true');args=ap.parse_args()
def version(ref,path):return subprocess.check_output(['git','show',ref+':'+path],cwd=ROOT,text=True)
def function(source):
 start=source.index('bool prepare() {');end=source.index('\n}',start)+2
 return source[start:end]
pre='' if args.current_only else function(version('ca66db298','src/runtime/drivers/InstalledProviderGraph.cpp')).replace('prepare()','pre_u1_prepare()')
post=function((ROOT/'src/runtime/drivers/InstalledProviderGraph.cpp').read_text()).replace('prepare()','post_u1_prepare()')
fixture=(ROOT/'test/resources/package_cdc_sd_migration_test.cpp').read_text().split('int main(')[0]
source=version(args.baseline,'src/runtime/packages/PackageCdcSdMigration.cpp') if args.baseline else (ROOT/'src/runtime/packages/PackageCdcSdMigration.cpp').read_text()
harness=r'''
#include <chrono>
#include "runtime/packages/PackageUseGate.h"
#include "runtime/packages/PackageMutationGate.h"
namespace RuntimeProviders {struct GraphV2 {explicit GraphV2(void* =nullptr){}};}
bool bootstrapHandoffComplete=true;
RuntimeProviders::GraphV2 resident;
RuntimeProviders::GraphV2 *graph=&resident;
void* nativeProviderStreamHost(){return nullptr;}
void poll(){}
void nativeProviderSetOwnerPoll(void(*)()){}
const auto kPolicy=policy;
'''+pre+'\n'+post+r'''
void measure(const char *name,bool(*call)(),size_t expected) {
 CdcSdTest::directoryOpens=CdcSdTest::entryReads=0;
 const auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<64;++i)assert(call());
 assert(CdcSdTest::entryReads==expected);
 double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 std::cout<<name<<" queries=64 entries="<<CdcSdTest::entryReads<<" directory_opens="<<CdcSdTest::directoryOpens<<" host_ms="<<ms<<'\n';
}
int main(int argc,char**argv){
 assert(argc==2);CdcSdTest::root=argv[1];CdcSdTest::cacheable=true;
 bootstrapHandoffComplete=false;
 assert(!post_u1_prepare() && CdcSdTest::directoryOpens==0);
 bootstrapHandoffComplete=true;
 std::filesystem::create_directories(CdcSdTest::root+"/Drivers");
 for(unsigned i=0;i<128;++i)std::filesystem::create_directories(CdcSdTest::root+"/Drivers/ordinary-"+std::to_string(i));
#ifdef RUN_PRE_U1
 measure("pre_u1_warm",pre_u1_prepare,0);
#endif
#ifdef EXPECT_CACHE
 measure("post_u1_first_and_warm",post_u1_prepare,129);
 measure("post_u1_warm",post_u1_prepare,0);
#else
 measure("post_u1_first_and_warm",post_u1_prepare,8256);
 measure("post_u1_warm",post_u1_prepare,8256);
#endif
 // Observed mutation must invalidate a negative observation before reuse.
 std::ofstream(CdcSdTest::root+kCdcIntentPath)<<"incomplete intent";Storage.invalidateObservations();
 assert(cdcMigrationPendingOnSd());
 std::filesystem::remove(CdcSdTest::root+kCdcIntentPath);Storage.invalidateObservations();
 assert(!cdcMigrationPendingOnSd());
 // No cache reuse with an active writer/uncertain external access.
 CdcSdTest::cacheable=false;CdcSdTest::failDirectoryRead="/Drivers";
 assert(cdcMigrationPendingOnSd());assert(cdcMigrationPendingOnSd());
 CdcSdTest::failDirectoryRead.clear();CdcSdTest::cacheable=true;Storage.invalidateObservations();
 assert(!cdcMigrationPendingOnSd());
 // Read/close errors are never negative observations.
 CdcSdTest::failClose="/Drivers";Storage.invalidateObservations();
 assert(cdcMigrationPendingOnSd());CdcSdTest::failClose.clear();assert(!cdcMigrationPendingOnSd());
 // Remount and mutation during a scan cannot reuse the prior observation.
 ++CdcSdTest::mount;CdcSdTest::entryReads=0;assert(!cdcMigrationPendingOnSd());
 assert(CdcSdTest::entryReads==129);
 CdcSdTest::mutateDuringRead=true;Storage.invalidateObservations();
 assert(!cdcMigrationPendingOnSd());CdcSdTest::mutateDuringRead=false;
 CdcSdTest::entryReads=0;assert(!cdcMigrationPendingOnSd());assert(CdcSdTest::entryReads==129);
 std::cout<<"Mutation/remount, active writer/uncertainty, failed read/close, mid-scan mutation: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'cdc.cpp').write_text(source);(p/'test.cpp').write_text(fixture+harness)
 cmd=['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-pthread','-Isrc','-Isrc/runtime/packages','-Itest/resources/cdc_sd_stubs',str(p/'cdc.cpp'),str(p/'test.cpp'),'-lcrypto','-o',str(p/'test')]
 if not args.baseline:cmd.insert(1,'-DEXPECT_CACHE')
 if not args.current_only:cmd.insert(1,'-DRUN_PRE_U1')
 subprocess.run(cmd,cwd=ROOT,check=True,timeout=60)
 subprocess.run([str(p/'test'),str(p/'media')],check=True,timeout=30)
