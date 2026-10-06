#!/usr/bin/env python3
"""Production TXT-cache/Serialization/HAL scheduling and exact I/O parity regression.

Builds the checked-in production function bodies and compares every request, byte,
lock, mutation, sync, error, open and close event with pinned pre-repair 36891e71.
Storage-provider, clock and scheduler endpoints are explicit deterministic fixtures.
This is not hardware timing or an SdFat integration test. --legacy checks the
non-volume caller branch and real non-volume cooperative forwarding methods.

Unchecked scalar reads are a pre-existing contract: a zero/partial final read can
leave automatic scalars indeterminate. Never execute those cache-reader cases.
Instead exercise complete invalid headers, complete copy-then-error reads, positive
short reads completed by HAL, and truncated tables rejected by the size check.
Zero/partial/error/deadline reads are separately exercised on initialized buffers.
The UINT32_MAX page-count case uses host size_t arithmetic; no new target-safe
page-count cap or 32-bit overflow-safety claim is made.
Writer failures are compared without deserializing damaged cache bytes.

Negative control: TXT_INDEX_SOURCE=/path/to/original/TxtReaderActivity.cpp python3
this_file.py --expect-optimized must fail the deterministic wait-count assertion.
Use --baseline with that override to measure the original instead. --sanitize
runs ASan/UBSan; leak detection alone is disabled for ptrace-host compatibility.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

BASELINE_FIXTURE_SHA256 = "660697fbd7e240ec23d22e92acaa7f9b3df573f7c1344c968d6952833732025c"
BASELINE = "36891e71ab651152c618d60d1d248abb4a7f44ff"
TXT_PATH = "src/activities/reader/TxtReaderActivity.cpp"
HAL_PATH = "lib/hal/HalStorageVolume.cpp"
SER_PATH = "lib/Serialization/Serialization.h"
EXPECTED = {
    TXT_PATH: "fb1013fe13654c05be10738196a058a753dcd3f369d578acd67f0f086ff6e503",
    HAL_PATH: "d28b659ac204cd698e28f4b4fdecad2a59710aae4189aff62213d33b6c7d1dfc",
    SER_PATH: "8a83ae01e6817eb6041c0aeeb3fd5892b932954f25f4f68b820bc0b42a3cf538",
}


def body(source, signature):
    """Extract an entire production definition, unchanged (balanced braces)."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


STUB = r'''
#pragma once
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <HalReadBudget.h>
#include <HalWriteBudget.h>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
struct Counts {
 size_t reads=0,writes=0,readBytes=0,writeBytes=0,locks=0,mutations=0,syncs=0;
 size_t errors=0,opens=0,closes=0,uncertain=0,delays=0,yields=0;
};
Counts counts;
std::vector<uint8_t> trace,cache;
uint64_t clockMs=0,lastYield=0,maxYieldGap=0;
uint32_t providerMillis=0;
size_t maxRead=SIZE_MAX,shortCall=SIZE_MAX,zeroCall=SIZE_MAX,errorCall=SIZE_MAX,syncFault=SIZE_MAX;
bool ready=true,lockFails=false,openFails=false,closeFails=false,providerError=false,stickyError=false,syncWrites=false;
unsigned live=0,nextHandle=1,lastError=0;
std::map<unsigned,size_t> positions;
static void raw(const void* p,size_t n){if(n){const auto* b=static_cast<const uint8_t*>(p);trace.insert(trace.end(),b,b+n);}}
static void event(uint64_t tag,std::initializer_list<uint64_t> values={}){raw(&tag,sizeof(tag));for(auto v:values)raw(&v,sizeof(v));}
uint32_t millis(){return static_cast<uint32_t>(clockMs);}
void waitOne(){maxYieldGap=std::max(maxYieldGap,clockMs-lastYield);++clockMs;lastYield=clockMs;}
void delay(unsigned n){assert(n==1);++counts.delays;waitOne();}
void vTaskDelay(unsigned n){assert(n==1);++counts.yields;waitOne();}
bool mediaReady(){return ready;}
constexpr size_t kMaxOperationBytes=16u*1024u*1024u,RISC_STORAGE_VOLUME_IO_MAX=4096;
constexpr uint32_t kOperationMs=20000;
class HalFile;
HalFile* currentFile=nullptr;
size_t providerRead(void*,uint32_t,uint8_t*,size_t);
size_t providerWrite(void*,uint32_t,const uint8_t*,size_t);
bool providerClose(void*,uint32_t,bool);
bool providerErrorRead(void*,uint32_t,bool);
bool providerSync(void*,uint32_t);
bool providerInfo(void*,uint32_t,uint64_t*,uint64_t*);
struct Volume {
 void* context=nullptr;
 decltype(&providerRead) file_read=providerRead;
 decltype(&providerWrite) file_write=providerWrite;
 decltype(&providerClose) file_close=providerClose;
} v;
Volume* volume=&v;
struct Extended {
 decltype(&providerErrorRead) handle_error=providerErrorRead;
 decltype(&providerSync) file_sync=providerSync;
 decltype(&providerInfo) file_info=providerInfo;
 bool (*dir_close_checked)(void*,uint32_t)=[](void* p,uint32_t h){return providerClose(p,h,true);};
} e;
Extended* extended=&e;
struct Generations {
 bool opened(bool writer){event(10,{writer});return true;}
 void closed(bool writer){event(11,{writer});}
 void mutationAttempt(){++counts.mutations;event(12);}
 void externalUncertain(){++counts.uncertain;event(13);}
} generations;
class HalFile {
 public:
 class Impl;
 std::unique_ptr<Impl> impl;
 HalFile(); ~HalFile();
 int read(void*,size_t);size_t write(const void*,size_t);
 int readWithBudget(void*,size_t,HalReadBudget*);
 int readCooperatively(void*,size_t,HalReadBudget&);
 size_t writeWithBudget(const void*,size_t,HalWriteBudget*);
 size_t writeCooperatively(const void*,size_t,HalWriteBudget&);
 size_t size();uint64_t fileSize64();bool close();
};
using FsFile=HalFile;
struct HalStorage {
 struct StorageLock {
  StorageLock(){++counts.locks;event(1,{!lockFails});}
  ~StorageLock(){event(2);}
  explicit operator bool()const{return !lockFails;}
 };
 bool openFileForRead(const char*,const std::string&,HalFile&);
 bool openFileForWrite(const char*,const std::string&,HalFile&);
} Storage;
'''

PROVIDERS = r'''
static bool openFixture(const std::string& path,HalFile& f,bool writer){
 ++counts.opens;event(3,{writer,!openFails});assert(path=="/fixture/index.bin");
 if(openFails)return false;
 if(writer)cache.clear();
 const unsigned id=nextHandle++;positions[id]=0;++live;
 f.impl=std::make_unique<HalFile::Impl>(id,false,writer,path.c_str());
 f.impl->syncWrites=syncWrites;currentFile=&f;return true;
}
bool HalStorage::openFileForRead(const char*,const std::string& p,HalFile& f){return openFixture(p,f,false);}
bool HalStorage::openFileForWrite(const char*,const std::string& p,HalFile& f){return openFixture(p,f,true);}
size_t providerRead(void*,uint32_t id,uint8_t* b,size_t want){
 ++counts.reads;clockMs+=providerMillis;assert(want<=4096);auto& at=positions.at(id);
 const size_t available=at<cache.size()?cache.size()-at:0;
 const size_t got=counts.reads==zeroCall?0:std::min({want,available,maxRead});
 event(4,{id,at,want,got});if(got)std::memcpy(b,cache.data()+at,got);
 at+=got;counts.readBytes+=got;
 providerError=(stickyError&&providerError)||counts.reads==errorCall;return got;
}
size_t providerWrite(void*,uint32_t id,const uint8_t* b,size_t want){
 ++counts.writes;clockMs+=providerMillis;assert(want<=4096);auto& at=positions.at(id);
 const size_t got=counts.writes==zeroCall?0:counts.writes==shortCall?want/2:want;
 event(5,{id,at,want,got});raw(b,want);
 if(at+got>cache.size())cache.resize(at+got);if(got)std::memcpy(cache.data()+at,b,got);
 at+=got;counts.writeBytes+=got;
 providerError=(stickyError&&providerError)||counts.writes==errorCall;return got;
}
bool providerErrorRead(void*,uint32_t id,bool dir){++counts.errors;event(6,{id,dir,providerError});return providerError;}
bool providerSync(void*,uint32_t id){++counts.syncs;const bool ok=counts.syncs!=syncFault;event(7,{id,ok});return ok;}
bool providerInfo(void*,uint32_t id,uint64_t* size,uint64_t* pos){event(8,{id});*size=cache.size();*pos=positions.at(id);return true;}
bool providerClose(void*,uint32_t id,bool flush){
 ++counts.closes;lastError=currentFile&&currentFile->impl?currentFile->impl->error:0;
 event(9,{id,flush,!closeFails,lastError});if(closeFails)return false;
 assert(positions.erase(id)==1);--live;return true;
}
// CACHE_CONSTANTS_FROM_PRODUCTION
struct Txt {
 size_t fileSize=0;
 std::string getCachePath()const{return "/fixture";}
 size_t getFileSize()const{return fileSize;}
};
class TxtReaderActivity {
 public:
 std::unique_ptr<Txt> txt=std::make_unique<Txt>();
 std::vector<size_t> pageOffsets;
 int totalPages=0,viewportWidth=400,linesPerPage=24,cachedFontId=1;
 uint8_t cachedScreenMargin=4,cachedParagraphAlignment=0;
 bool loadPageIndexCache();void savePageIndexCache()const;
};
'''

TESTS = r'''
static void reset(){
 assert(live==0);counts={};trace.clear();positions.clear();nextHandle=1;lastError=0;currentFile=nullptr;
 clockMs=lastYield=maxYieldGap=0;providerMillis=0;
 maxRead=shortCall=zeroCall=errorCall=syncFault=SIZE_MAX;
 ready=true;lockFails=openFails=closeFails=providerError=stickyError=syncWrites=false;
}
static std::ofstream snapshot;
static void record(const void* p,size_t n){uint64_t len=n;snapshot.write(reinterpret_cast<const char*>(&len),8);if(n)snapshot.write(static_cast<const char*>(p),n);assert(snapshot.good());}
static void capture(const std::string& name,const TxtReaderActivity& a,int result){
 record(name.data(),name.size());record(cache.data(),cache.size());record(trace.data(),trace.size());
 record(a.pageOffsets.data(),a.pageOffsets.size()*sizeof(size_t));
 const uint64_t summary[]={uint64_t(result),uint64_t(a.totalPages),counts.reads,counts.writes,counts.readBytes,counts.writeBytes,counts.locks,counts.mutations,counts.syncs,counts.errors,counts.opens,counts.closes,counts.uncertain,live,lastError};
 record(summary,sizeof(summary));
}
static TxtReaderActivity pageSet(unsigned n){
 TxtReaderActivity a;a.txt->fileSize=n*1200;a.totalPages=n;
 for(unsigned i=0;i<n;++i)a.pageOffsets.push_back(i*1200);
 return a;
}
static std::vector<uint8_t> expectedBytes(const TxtReaderActivity& a){
 std::vector<uint8_t> b;
 const auto pod=[&](const auto& v){const auto* p=reinterpret_cast<const uint8_t*>(&v);b.insert(b.end(),p,p+sizeof(v));};
 pod(CACHE_MAGIC);pod(CACHE_VERSION);pod(uint32_t(a.txt->fileSize));pod(int32_t(a.viewportWidth));pod(int32_t(a.linesPerPage));pod(int32_t(a.cachedFontId));pod(int32_t(a.cachedScreenMargin));pod(a.cachedParagraphAlignment);pod(uint32_t(a.pageOffsets.size()));
 for(size_t offset:a.pageOffsets)pod(uint32_t(offset));return b;
}
static void cost(const std::string& name,size_t requests){
 const size_t waits=counts.delays+counts.yields;
#ifdef EXPECT_OPTIMIZED
 assert(counts.delays==0&&counts.yields==requests/32&&"TXT cache still waits per scalar I/O");
#else
 assert(counts.delays==requests&&counts.yields==0);
#endif
 std::cout<<name<<" requests="<<requests<<" bytes="<<counts.readBytes+counts.writeBytes<<" waits="<<waits<<" ordinary="<<counts.delays<<" budget="<<counts.yields<<"\n";
}
static void retryRead(TxtReaderActivity& a,const std::vector<uint8_t>& good,const std::vector<size_t>& offsets){
 reset();cache=good;assert(a.loadPageIndexCache());assert(a.pageOffsets==offsets);assert(counts.opens==1&&counts.closes==1&&!live&&!lastError);
}
static void healthy(unsigned n){
 reset();auto writer=pageSet(n);writer.savePageIndexCache();const auto good=expectedBytes(writer);
 assert(cache==good&&cache.size()==30+4*n&&counts.writes==9+n&&counts.mutations==counts.writes+1);
 assert(counts.closes==1&&!live);cost("save:"+std::to_string(n),9+n);capture("save:"+std::to_string(n),writer,1);
 auto reader=pageSet(n);reader.pageOffsets={991,992};reader.totalPages=2;
 for(unsigned run=0;run<3;++run){
  reset();assert(reader.loadPageIndexCache());assert(reader.pageOffsets==writer.pageOffsets&&reader.totalPages==int(n));
  assert(cache==good&&counts.reads==9+n&&counts.readBytes==good.size()&&counts.mutations==0&&counts.opens==1&&counts.closes==1&&!live);
  cost("reopen:"+std::to_string(n)+":"+std::to_string(run),9+n);capture("reopen:"+std::to_string(n)+":"+std::to_string(run),reader,1);
 }
}
static void completeHeaderRejections(){
 const auto writer=pageSet(100);const auto good=expectedBytes(writer);
 // Only the first seven fields are validated in production. Alignment is read
 // and ignored; preserve that behavior rather than silently inventing validation.
 const unsigned starts[]={0,4,5,9,13,17,21};
 for(unsigned field=0;field<7;++field){
  reset();cache=good;cache[starts[field]]^=0x7f;auto reader=pageSet(100);reader.pageOffsets={711};reader.totalPages=91;
  assert(!reader.loadPageIndexCache());assert(reader.pageOffsets==std::vector<size_t>{711}&&reader.totalPages==91);
  assert(counts.reads==field+1&&counts.closes==1&&!live);capture("header-reject:"+std::to_string(field),reader,0);retryRead(reader,good,writer.pageOffsets);
 }
 for(unsigned kind=0;kind<5;++kind){
  reset();cache=good;
  if(kind==0){uint32_t zero=0;std::memcpy(cache.data()+26,&zero,4);}
  if(kind==1)cache.resize(30); // complete header, absent table
  if(kind==2)cache.resize(good.size()-1); // complete header, truncated table
  // Host-size_t arithmetic only: ESP32 32-bit overflow behavior is outside this scheduling test.
  if(kind==3){uint32_t huge=UINT32_MAX;std::memcpy(cache.data()+26,&huge,4);}
  if(kind==4)cache[25]=0xff; // alignment has no rejection in the original
  auto reader=pageSet(100);reader.pageOffsets={712};reader.totalPages=92;
  const bool ok=reader.loadPageIndexCache();assert(ok==(kind==4));assert(counts.closes==1&&!live);
  if(ok)assert(reader.pageOffsets==writer.pageOffsets);else{assert(counts.reads==9);assert(reader.pageOffsets==std::vector<size_t>{712}&&reader.totalPages==92);}
  capture("table-check:"+std::to_string(kind),reader,ok);retryRead(reader,good,writer.pageOffsets);
 }
 // No writer-side zero-page rejection exists. The complete zero-page header is
 // byte-compatible and its next read is rejected without reading any offset.
 reset();auto empty=pageSet(0);empty.savePageIndexCache();assert(cache==expectedBytes(empty));capture("empty-save",empty,1);
 reset();assert(!empty.loadPageIndexCache());assert(counts.reads==9&&!live);capture("empty-load",empty,0);
}
static void shortSlowAndErrors(){
 auto writer=pageSet(100);const auto good=expectedBytes(writer);
 for(size_t cap:{size_t(1),size_t(2),size_t(3)}){
  reset();cache=good;maxRead=cap;auto reader=pageSet(100);assert(reader.loadPageIndexCache()&&reader.pageOffsets==writer.pageOffsets&&!live);
  capture("positive-short:"+std::to_string(cap),reader,1);retryRead(reader,good,writer.pageOffsets);
 }
 for(unsigned slow:{3u,9u}){
  reset();cache=good;providerMillis=slow;auto reader=pageSet(100);assert(reader.loadPageIndexCache()&&reader.pageOffsets==writer.pageOffsets&&!live);
  assert(maxYieldGap<=8+slow);capture("slow-read:"+std::to_string(slow),reader,1);
  reset();providerMillis=slow;writer.savePageIndexCache();assert(cache==good&&!live&&maxYieldGap<=8+slow);capture("slow-write:"+std::to_string(slow),writer,1);
 }
 // Every provider call copies the complete requested scalar before setting the
 // error. All scalars therefore remain initialized despite legacy ignored returns.
 for(bool sticky:{false,true})for(size_t call:{1u,2u,9u,10u,32u,109u}){
  reset();cache=good;errorCall=call;stickyError=sticky;auto reader=pageSet(100);
  assert(reader.loadPageIndexCache()&&reader.pageOffsets==writer.pageOffsets&&lastError==1&&!live);
  capture("read-copy-error:"+std::to_string(sticky)+":"+std::to_string(call),reader,1);retryRead(reader,good,writer.pageOffsets);
 }
 for(unsigned kind=0;kind<5;++kind)for(size_t call:{1u,2u,9u,10u,32u,109u}){
  reset();
  if(kind==0)shortCall=call;
  if(kind==1)zeroCall=call;
  if(kind==2)errorCall=call;
  if(kind==3){errorCall=call;stickyError=true;}
  if(kind==4){syncWrites=true;syncFault=call;}
  writer.savePageIndexCache();assert(counts.writes==109&&counts.closes==1&&!live&&lastError==1);
  capture("write-fault:"+std::to_string(kind)+":"+std::to_string(call),writer,1);
  reset();writer.savePageIndexCache();assert(cache==good&&!lastError&&!live);retryRead(writer,good,writer.pageOffsets);
 }
 reset();syncWrites=true;writer.savePageIndexCache();assert(cache==good&&counts.syncs==109&&!live);capture("sync-write",writer,1);
}
static void openCloseFailures(){
 auto writer=pageSet(100);const auto good=expectedBytes(writer);
 for(bool writing:{false,true}){
  reset();cache=good;openFails=true;auto reader=pageSet(100);
  if(writing)writer.savePageIndexCache();else assert(!reader.loadPageIndexCache());
  assert(cache==good&&counts.opens==1&&counts.closes==0&&!live&&counts.reads==0&&counts.writes==0);
  capture("open-fault:"+std::to_string(writing),reader,0);retryRead(reader,good,writer.pageOffsets);
  reset();cache=good;closeFails=true;
  if(writing)writer.savePageIndexCache();else assert(reader.loadPageIndexCache());
  assert(live==1&&counts.closes==1&&counts.uncertain==1);capture("close-fault:"+std::to_string(writing),reader,1);
  // Real destructor cannot retry after it is gone: the failed provider slot is
  // retained. A later cache open gets a new handle and must not close that slot.
  closeFails=false;providerError=false;currentFile=nullptr;
  assert(reader.loadPageIndexCache()&&reader.pageOffsets==writer.pageOffsets);assert(live==1&&counts.closes==2);
  capture("reopen-after-close-fault:"+std::to_string(writing),reader,1);
  // Explicit fixture teardown of the retained slot, not invented HAL recovery.
  currentFile=nullptr;assert(providerClose(nullptr,1,true));assert(!live);
 }
 // A live caller can retry an explicit failed close. Use real close()/Impl body.
 reset();cache=good;{FsFile f;assert(Storage.openFileForRead("TRS","/fixture/index.bin",f));closeFails=true;assert(!f.close()&&f.impl->handle&&f.impl->error);closeFails=false;assert(f.close()&&!f.impl->handle);}assert(!live&&counts.closes==2&&counts.uncertain==1);
}
static void ordinaryControls(){
 // Unrelated ordinary serializers retain one delay per provider progress event.
 for(bool writing:{false,true}){
  reset();cache.assign(4*100,0x55);FsFile f;
  assert(writing?Storage.openFileForWrite("TRS","/fixture/index.bin",f):Storage.openFileForRead("TRS","/fixture/index.bin",f));
  for(unsigned i=0;i<100;++i){uint32_t x=0x55555555;if(writing)serialization::writePod(f,x);else{serialization::readPod(f,x);assert(x==0x55555555);}}
  assert(counts.delays==100&&counts.yields==0&&counts.locks==100);assert(f.close());
  auto a=pageSet(0);capture("ordinary:"+std::to_string(writing),a,1);
 }
}
#ifndef ORIGINAL_HAL
static void halControls(){
 for(bool writing:{false,true})for(unsigned fault=0;fault<15;++fault){
  std::vector<uint8_t> oldTrace,oldBytes,oldBuffer;int64_t oldResult=0;unsigned oldError=0;
  for(bool cooperative:{false,true}){
   reset();cache.assign(5*4096,0x55);FsFile f;assert(writing?Storage.openFileForWrite("TRS","/fixture/index.bin",f):Storage.openFileForRead("TRS","/fixture/index.bin",f));
   std::vector<uint8_t> bytes(5*4096,writing?0x55:0xcc);void* ptr=bytes.data();size_t size=bytes.size();
   if(fault==0)lockFails=true;if(fault==1)ready=false;if(fault==2)f.impl->directory=true;
   if(fault==3)f.impl->writer=false;if(fault==4)f.impl->handle=0;if(fault==5)ptr=nullptr;
   if(fault==6)size=kMaxOperationBytes+1;if(fault==7){shortCall=2;maxRead=7;}
   if(fault==8)zeroCall=1;if(fault==9)errorCall=2;if(fault==10){syncWrites=true;f.impl->syncWrites=true;syncFault=2;}
   if(fault==11)providerMillis=5000;if(fault==12)size=0;if(fault==13){errorCall=1;stickyError=true;}
   if(fault==14)f.impl->error=1;
   HalReadBudget rb([](){return millis();},[](){vTaskDelay(1);});HalWriteBudget wb([](){return millis();},[](){vTaskDelay(1);});
   int64_t result=writing?(cooperative?f.writeCooperatively(ptr,size,wb):f.write(ptr,size)):(cooperative?f.readCooperatively(ptr,size,rb):f.read(ptr,size));
   const unsigned error=f.impl->error;
   if(!cooperative){oldTrace=trace;oldBytes=cache;oldBuffer=bytes;oldResult=result;oldError=error;}
   else assert(trace==oldTrace&&cache==oldBytes&&bytes==oldBuffer&&result==oldResult&&error==oldError);
   if(fault==11)assert(result==4*4096&&error==1); // unchanged per-call 20 s deadline
   lockFails=false;ready=true;f.impl->handle=1;f.impl->directory=false;assert(f.close());
  }
 }
 for(bool writing:{false,true}){
  reset();FsFile missing;HalReadBudget rb([](){return millis();},[](){vTaskDelay(1);});HalWriteBudget wb([](){return millis();},[](){vTaskDelay(1);});
  assert(writing?missing.writeCooperatively(nullptr,0,wb)==0:missing.readCooperatively(nullptr,0,rb)==-1);
  assert(counts.locks==1&&!counts.reads&&!counts.writes&&!counts.mutations);
 }
#ifndef LEGACY_CALLER
 // Pre/post checkpoints cover guard failures, zero progress and error progress.
 for(bool writing:{false,true})for(unsigned fault=0;fault<3;++fault){
  reset();cache.assign(1,0x55);FsFile f;assert(writing?Storage.openFileForWrite("TRS","/fixture/index.bin",f):Storage.openFileForRead("TRS","/fixture/index.bin",f));
  HalReadBudget rb([](){return millis();},[](){vTaskDelay(1);});HalWriteBudget wb([](){return millis();},[](){vTaskDelay(1);});uint8_t byte=0;
  clockMs=8;if(fault==0)lockFails=true;else{providerMillis=8;if(fault==1)zeroCall=1;else errorCall=1;}
  if(writing)f.writeCooperatively(&byte,1,wb);else f.readCooperatively(&byte,1,rb);
  assert(counts.yields==(fault==0?1u:2u));lockFails=false;assert(f.close());
 }
#endif
}
#endif
static void primitiveControls(){
 for(unsigned writing=0;writing<2;++writing)for(unsigned test=0;test<5;++test){
  reset();if(test==3)clockMs=UINT32_MAX-3ull;
  HalReadBudget rb([](){return millis();},[](){vTaskDelay(1);});HalWriteBudget wb([](){return millis();},[](){vTaskDelay(1);});
  const auto after=[&](size_t n){if(writing)wb.afterWrite(n);else rb.afterRead(n);};
  const auto checkpoint=[&](){if(writing)wb.checkpoint();else rb.checkpoint();};
  if(test==0){for(unsigned i=0;i<31;++i)after(1);assert(!counts.yields);after(1);assert(counts.yields==1);}
  if(test==1){after(4095);assert(!counts.yields);after(1);assert(counts.yields==1);}
  if(test==2){clockMs=7;checkpoint();assert(!counts.yields);clockMs=8;checkpoint();assert(counts.yields==1);}
  if(test==3){clockMs+=8;checkpoint();assert(counts.yields==1);}
  if(test==4){for(unsigned i=0;i<100;++i){clockMs+=2;checkpoint();}assert(counts.yields==25&&maxYieldGap==8);}
 }
 reset();HalReadBudget rows([](){return millis();},[](){vTaskDelay(1);});for(unsigned i=0;i<32;++i)rows.afterRow();assert(counts.yields==1);
}
int main(int argc,char** argv){
 assert(CACHE_MAGIC==0x54585449&&CACHE_VERSION==4);
 assert(argc==2);snapshot.open(argv[1],std::ios::binary);assert(snapshot.good());
 for(unsigned n:{1u,100u,1000u,10000u})healthy(n);
 completeHeaderRejections();shortSlowAndErrors();openCloseFailures();ordinaryControls();
 snapshot.close();
#ifndef ORIGINAL_HAL
 halControls();
#endif
 primitiveControls();
 std::cout<<"PASS: cache bytes, requests, state, faults, retry/cleanup and budget bounds\n";
}
'''


def cache_constants(source):
    return "\n".join(re.search(r"constexpr\s+uint(?:32|8)_t\s+" + name + r"\s*=.*?;", source).group(0)
                     for name in ("CACHE_MAGIC", "CACHE_VERSION"))


def baseline_fragments(sources):
    hal, txt = sources[HAL_PATH], sources[TXT_PATH]
    definitions = body(hal, "class HalFile::Impl") + ";\n"
    for signature in ("HalFile::~HalFile()", "bool HalFile::close()", "uint64_t HalFile::fileSize64()", "size_t HalFile::size()",
                      "int HalFile::read(void *buffer, size_t count)", "size_t HalFile::write(const void *buffer, size_t count)"):
        definitions += body(hal, signature)
    return {HAL_PATH: definitions,
            TXT_PATH: cache_constants(txt) + "\n" + body(txt, "bool TxtReaderActivity::loadPageIndexCache()")
                      + body(txt, "void TxtReaderActivity::savePageIndexCache() const"),
            SER_PATH: sources[SER_PATH]}


def harness(hal, txt, legacy_hal=None):
    definitions = body(hal, "class HalFile::Impl") + ";\n"
    definitions += "HalFile::HalFile() = default;\n"
    definitions += body(hal, "HalFile::~HalFile()")
    for signature in ("bool HalFile::close()", "uint64_t HalFile::fileSize64()", "size_t HalFile::size()"):
        definitions += body(hal, signature)
    for signature in ("int HalFile::read(void *buffer, size_t count)",
                      "int HalFile::readCooperatively(", "int HalFile::readWithBudget(",
                      "size_t HalFile::write(const void *buffer, size_t count)",
                      "size_t HalFile::writeCooperatively(", "size_t HalFile::writeWithBudget("):
        if signature in hal:
            source = legacy_hal if legacy_hal and "Cooperatively(" in signature else hal
            definitions += body(source, signature)
    return '#include "HalStorage.h"\n#include "Serialization.h"\n' + definitions + PROVIDERS.replace("// CACHE_CONSTANTS_FROM_PRODUCTION", cache_constants(txt)) + body(txt, "bool TxtReaderActivity::loadPageIndexCache()") + body(txt, "void TxtReaderActivity::savePageIndexCache() const") + TESTS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--verify-baseline-git", action="store_true", help="verify the pinned fixture against full original Git blobs when available")
    parser.add_argument("--legacy", action="store_true")
    parser.add_argument("--board", choices=("BOARD_XTEINK_X4_PRO", "BOARD_T5S3_PRO"), default="BOARD_XTEINK_X4_PRO")
    parser.add_argument("--source", type=Path, default=os.environ.get("TXT_INDEX_SOURCE"))
    parser.add_argument("--hal-source", type=Path, default=os.environ.get("TXT_INDEX_HAL_SOURCE"))
    expectations = parser.add_mutually_exclusive_group()
    expectations.add_argument("--baseline", action="store_true")
    expectations.add_argument("--expect-optimized", action="store_true")
    parser.add_argument("--snapshot", type=Path, help="save the exact tested binary parity snapshot")
    args = parser.parse_args()
    if args.legacy and args.expect_optimized:
        parser.error("--legacy intentionally retains ordinary scheduling")
    root = Path(__file__).resolve().parents[2]
    fixture_path = Path(__file__).with_name("fixtures") / "txt_index_36891e71.json"
    fixture_bytes = fixture_path.read_bytes()
    assert hashlib.sha256(fixture_bytes).hexdigest() == BASELINE_FIXTURE_SHA256, "Pinned baseline fixture changed"
    fixture = json.loads(fixture_bytes)
    assert fixture["commit"] == BASELINE and fixture["full_source_sha256"] == EXPECTED
    original = fixture["fragments"]
    if args.verify_baseline_git:
        full_sources = {}
        for path, expected in EXPECTED.items():
            data = subprocess.check_output(["git", "show", f"{BASELINE}:{path}"], cwd=root)
            assert hashlib.sha256(data).hexdigest() == expected, f"Pinned production source changed: {path}"
            full_sources[path] = data.decode()
        assert baseline_fragments(full_sources) == original, "Baseline fragments do not match pinned original production"
        print("PASS: fixture verified against exact pinned Git blobs")
    txt = (args.source or root / TXT_PATH).read_text()
    hal = (args.hal_source or root / HAL_PATH).read_text()
    legacy_hal = (root / "lib/hal/HalStorage.cpp").read_text() if args.legacy else None
    flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-misleading-indentation"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    env = os.environ.copy()
    if args.sanitize:
        env["ASAN_OPTIONS"] = env.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
        env["UBSAN_OPTIONS"] = env.get("UBSAN_OPTIONS", "") + ":halt_on_error=1"
    with tempfile.TemporaryDirectory(prefix="txt-index-io-") as directory:
        temp = Path(directory)
        snapshots = []
        for name, source_txt, source_hal, serialization in (
            ("original", original[TXT_PATH], original[HAL_PATH], original[SER_PATH]),
            ("current", txt, hal, (root / SER_PATH).read_text()),
        ):
            build = temp / name
            build.mkdir()
            (build / "HalStorage.h").write_text(STUB)
            (build / "Serialization.h").write_text(serialization)
            (build / "test.cpp").write_text(harness(source_hal, source_txt, legacy_hal if name == "current" else None))
            defines = []
            if "int HalFile::readCooperatively(" not in source_hal:
                defines += ["-DORIGINAL_HAL=1"]
            if args.legacy:
                defines += ["-DLEGACY_CALLER=1"]
            else:
                defines += ["-D" + args.board + "=1"]
            if name == "current" and not args.legacy and not args.baseline:
                defines += ["-DEXPECT_OPTIMIZED=1"]
            binary, snapshot = build / "test", build / "snapshot.bin"
            subprocess.run([os.environ.get("CXX", "c++"), *flags, *defines, "-I" + str(build), "-I" + str(root / "lib/hal"), str(build / "test.cpp"), "-o", str(binary)], check=True, timeout=120)
            print(f"--- {name} {'legacy' if args.legacy else args.board} ---", flush=True)
            subprocess.run([str(binary), str(snapshot)], check=True, timeout=120, env=env)
            snapshots.append(snapshot)
        before, after = (p.read_bytes() for p in snapshots)
        assert before == after, "Production cache bytes/request/lock/mutation/sync/error/cleanup traces differ from baseline"
        if args.snapshot:
            shutil.copyfile(snapshots[1], args.snapshot)
        print(f"PASS: exact original/current snapshot equality ({len(after)} bytes, SHA256 {hashlib.sha256(after).hexdigest()})")
        print("LIMIT: incomplete unchecked scalar reads excluded; provider/clock/mutex/open setup are fixtures; no device-latency claim.")


if __name__ == "__main__":
    main()
