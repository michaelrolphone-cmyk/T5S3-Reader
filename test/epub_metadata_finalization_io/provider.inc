// Complete production EPUB metadata/parsers/ZIP and volume HAL are linked.
// Only storage endpoints, heap, scheduler and mutex are fixtures.
#include <HalStorage.h>
#include <HalReadBudget.h>
#if HAL_HAS_WRITE_BUDGET
#include <HalWriteBudget.h>
#endif
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <vector>
#include "Epub/BookMetadataCache.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/ContainerParser.h"
#include <ZipFile.h>
#include <Serialization.h>
#include "Epub/ContentCachePaths.h"

using Bytes = std::vector<uint8_t>;
struct Handle { std::shared_ptr<Bytes> bytes; std::string path; size_t pos = 0; bool error = false; };
static std::map<std::string, std::shared_ptr<Bytes>> files;
static std::set<std::string> dirs;
static std::map<uint32_t, Handle> handles;
static uint32_t nextHandle = 1;
struct Counts { uint64_t reads=0, bytes=0, opens=0, closes=0, seeks=0, errors=0, writes=0, writeBytes=0, syncs=0; };
static Counts counts;
static std::map<std::string,Counts> byPath;
static bool recording = false, ready = true, openFails = false, closeFails = false, stickyError = false, syncFails = false;
static size_t maxRead=SIZE_MAX, maxWrite=SIZE_MAX, errorCall=SIZE_MAX, zeroCall=SIZE_MAX, seekFail=SIZE_MAX, dropMediaCall=SIZE_MAX;
static uint32_t latency = 0;
static std::string failOpenPath, failClosePath;
static bool writeOnlyFaults=false;
static size_t dropWriteCall=SIZE_MAX, shortWriteCall=SIZE_MAX;
static Bytes trace;
static void raw(const void* p, size_t n) { if (n) { auto b=static_cast<const uint8_t*>(p); trace.insert(trace.end(),b,b+n); } }
static void event(uint64_t tag, std::initializer_list<uint64_t> values={}) {
  if (!recording) return;
  raw(&tag,sizeof(tag)); const uint64_t size=values.size(); raw(&size,sizeof(size));
  for (auto value:values) raw(&value,sizeof(value));
}
static void pathEvent(const char* path) { if(recording){const uint64_t size=strlen(path);raw(&size,sizeof(size));raw(path,size);} }
static bool providerReady(void*) { event(1,{ready}); return ready; }
static bool providerStat(void*, const char* path, uint64_t* size, bool* directory) {
  const auto file=files.find(path); *directory=dirs.count(path); *size=file==files.end()?0:file->second->size();
  const bool ok=*directory||file!=files.end(); event(2,{ok,*size,*directory});pathEvent(path);return ok;
}
static uint32_t providerOpen(void*, const char* path, uint32_t flags) {
  ++counts.opens;
  if (openFails || failOpenPath==path) { event(3,{flags,0});pathEvent(path);return 0; }
  auto found=files.find(path);
  if (found==files.end()) { if (!(flags&RISC_STORAGE_OPEN_CREATE)) { event(3,{flags,0});pathEvent(path);return 0; } files[path]=std::make_shared<Bytes>(); }
  if (flags&RISC_STORAGE_OPEN_TRUNCATE) files.at(path)->clear();
  const auto h=nextHandle++; handles.emplace(h,Handle{files.at(path),path}); event(3,{flags,h});pathEvent(path);return h;
}
static size_t providerRead(void*, uint32_t id, void* output, size_t want) {
  auto& h=handles.at(id); ++counts.reads; ++byPath[h.path].reads; Fixture::clockMs+=latency; assert(want<=4096);
  const auto available=h.pos<h.bytes->size()?h.bytes->size()-h.pos:0;
  const size_t got=(!writeOnlyFaults&&counts.reads==zeroCall)?0:std::min({want,available,maxRead});
  event(4,{id,h.pos,want,got});
  if (got) { memcpy(output,h.bytes->data()+h.pos,got); if(recording) raw(output,got); }
  h.pos+=got; counts.bytes+=got; byPath[h.path].bytes+=got;
  h.error=(!writeOnlyFaults&&stickyError&&h.error)||(!writeOnlyFaults&&counts.reads==errorCall);
  if(counts.reads==dropMediaCall)ready=false;
  return got;
}
static size_t providerWrite(void*, uint32_t id, const void* input, size_t want) {
  auto& h=handles.at(id); ++counts.writes; ++byPath[h.path].writes; Fixture::clockMs+=latency; assert(want<=4096);
  const size_t got=counts.writes==zeroCall?0:counts.writes==shortWriteCall?want-1:std::min(want,maxWrite);
  if (h.pos+got>h.bytes->size()) h.bytes->resize(h.pos+got);
  if (got) memcpy(h.bytes->data()+h.pos,input,got);
  event(5,{id,h.pos,want,got}); if(recording) raw(input,want);
  h.pos+=got; counts.writeBytes+=got; byPath[h.path].writeBytes+=got; h.error=(stickyError&&h.error)||counts.writes==errorCall;
  if(counts.writes==dropWriteCall)ready=false;
  return got;
}
static bool providerSeek(void*, uint32_t id, uint64_t position) {
  auto& h=handles.at(id); ++counts.seeks;
  const bool ok=counts.seeks!=seekFail&&position<=h.bytes->size();
  event(6,{id,h.pos,position,ok}); if(ok)h.pos=position; return ok;
}
static bool providerInfo(void*, uint32_t id, uint64_t* size, uint64_t* position) {
  const auto& h=handles.at(id); *size=h.bytes->size(); *position=h.pos; event(7,{id,*size,*position}); return true;
}
static uint32_t providerError(void*, uint32_t id, bool dir) {
  ++counts.errors; const unsigned error=handles.at(id).error; event(8,{id,dir,error}); return error;
}
static bool providerClose(void*, uint32_t id, bool commit) {
  ++counts.closes; const auto& h=handles.at(id); event(9,{id,h.pos,commit,!(closeFails || failClosePath==h.path),h.error});
  if(closeFails || failClosePath==h.path)return false;
  assert(handles.erase(id)==1); return true;
}
static bool providerRemove(void*, const char* path) { event(10);pathEvent(path);return files.erase(path)||dirs.erase(path); }
static risc_storage_volume_api_v1_ext volume=[] {
  risc_storage_volume_api_v1_ext v{};
  v.base.api_version=1; v.base.struct_size=sizeof(v); v.base.refresh=[](void* context){event(14);return providerReady(context);}; v.base.ready=providerReady;
  v.base.stat=providerStat; v.base.file_read=providerRead; v.base.file_write=providerWrite; v.base.file_close=providerClose;
  v.base.remove=providerRemove;
  v.base.dir_open=[](void*,const char*)->uint32_t{assert(false);return 0;};
  v.base.dir_next=[](void*,uint32_t,risc_storage_dirent_v1*){assert(false);return false;};
  v.file_open=providerOpen; v.file_seek=providerSeek; v.file_info=providerInfo; v.handle_error=providerError;
  v.file_sync=[](void*,uint32_t id){++counts.syncs;event(11,{id,!syncFails});return !syncFails;};
  v.dir_rewind=[](void*,uint32_t){assert(false);return false;};
  v.dir_close_checked=[](void*,uint32_t){assert(false);return false;};
  v.mkdir=[](void*,const char* path){event(12,{1});pathEvent(path);dirs.insert(path);return true;};
  v.rename=[](void*,const char* from,const char* to){auto found=files.find(from);const bool ok=found!=files.end()&&!files.count(to);event(13,{ok});pathEvent(from);pathEvent(to);if(!ok)return false;files[to]=found->second;files.erase(found);return true;};
  return v;
}();
