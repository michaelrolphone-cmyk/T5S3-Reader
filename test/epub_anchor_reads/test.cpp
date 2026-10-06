// Complete production Section/parser/layout and complete volume HAL are linked.
// Only the capability endpoint, extraction, fonts, clock and mutex are fixtures.
#include <HalStorage.h>
#include <HalReadBudget.h>
#include <GfxRenderer.h>
#include <Serialization.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include "Epub.h"
#include "Epub/Section.h"
#include "Epub/Page.h"
#include "Epub/hyphenation/Hyphenator.h"
#include "Epub/converters/ImageDecoderFactory.h"

using Bytes = std::vector<uint8_t>;
struct Handle { std::shared_ptr<Bytes> bytes; std::string path; size_t pos = 0; bool error = false; };
static std::map<std::string, std::shared_ptr<Bytes>> files;
static std::set<std::string> dirs;
static std::map<uint32_t, Handle> handles;
static uint32_t nextHandle = 1;
struct Counts { uint64_t reads=0, bytes=0, opens=0, closes=0, seeks=0, errors=0, writes=0; };
static Counts counts;
static bool recording = false, ready = true, openFails = false, closeFails = false, stickyError = false;
static size_t maxRead=SIZE_MAX, errorCall=SIZE_MAX, zeroCall=SIZE_MAX, seekFail=SIZE_MAX;
static uint32_t latency = 0;
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
  if (openFails) { event(3,{flags,0});pathEvent(path);return 0; }
  auto found=files.find(path);
  if (found==files.end()) { if (!(flags&RISC_STORAGE_OPEN_CREATE)) { event(3,{flags,0});pathEvent(path);return 0; } files[path]=std::make_shared<Bytes>(); }
  if (flags&RISC_STORAGE_OPEN_TRUNCATE) files.at(path)->clear();
  const auto h=nextHandle++; handles.emplace(h,Handle{files.at(path),path}); event(3,{flags,h});pathEvent(path);return h;
}
static size_t providerRead(void*, uint32_t id, void* output, size_t want) {
  auto& h=handles.at(id); ++counts.reads; Fixture::clockMs+=latency; assert(want<=4096);
  const auto available=h.pos<h.bytes->size()?h.bytes->size()-h.pos:0;
  const size_t got=counts.reads==zeroCall?0:std::min({want,available,maxRead});
  event(4,{id,h.pos,want,got});
  if (got) { memcpy(output,h.bytes->data()+h.pos,got); if(recording) raw(output,got); }
  h.pos+=got; counts.bytes+=got;
  h.error=(stickyError&&h.error)||counts.reads==errorCall;
  return got;
}
static size_t providerWrite(void*, uint32_t id, const void* input, size_t count) {
  auto& h=handles.at(id); ++counts.writes;
  if (h.pos+count>h.bytes->size()) h.bytes->resize(h.pos+count);
  if (count) memcpy(h.bytes->data()+h.pos,input,count);
  event(5,{id,h.pos,count}); h.pos+=count; return count;
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
  ++counts.closes; const auto& h=handles.at(id); event(9,{id,h.pos,commit,!closeFails,h.error});
  if(closeFails)return false;
  assert(handles.erase(id)==1); return true;
}
static bool providerRemove(void*, const char* path) { event(10);pathEvent(path);return files.erase(path)||dirs.erase(path); }
static risc_storage_volume_api_v1_ext volume=[] {
  risc_storage_volume_api_v1_ext v{};
  v.base.api_version=1; v.base.struct_size=sizeof(v); v.base.refresh=providerReady; v.base.ready=providerReady;
  v.base.stat=providerStat; v.base.file_read=providerRead; v.base.file_write=providerWrite; v.base.file_close=providerClose;
  v.base.remove=providerRemove;
  v.base.dir_open=[](void*,const char*)->uint32_t{assert(false);return 0;};
  v.base.dir_next=[](void*,uint32_t,risc_storage_dirent_v1*){assert(false);return false;};
  v.file_open=providerOpen; v.file_seek=providerSeek; v.file_info=providerInfo; v.handle_error=providerError;
  v.file_sync=[](void*,uint32_t){return true;};
  v.dir_rewind=[](void*,uint32_t){assert(false);return false;};
  v.dir_close_checked=[](void*,uint32_t){assert(false);return false;};
  v.mkdir=[](void*,const char* path){dirs.insert(path);return true;};
  v.rename=[](void*,const char* from,const char* to){auto found=files.find(from);if(found==files.end()||files.count(to))return false;files[to]=found->second;files.erase(found);return true;};
  return v;
}();

const std::string& Epub::getCachePath() const { return cachePath; }
const std::string& Epub::getLanguage() const { static std::string language="en"; return language; }
BookMetadataCache::SpineEntry Epub::getSpineItem(int) const { BookMetadataCache::SpineEntry item; item.href="chapter.xhtml";return item; }
bool Epub::readItemContentsToStream(const std::string& path, Print& output, size_t) const {
  const auto& data=*files.at(path); return output.write(data.data(),data.size())==data.size();
}
void Hyphenator::setPreferredLanguage(const std::string&) {}
std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string&,bool){assert(false);return {};}
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&){assert(false);return nullptr;}
bool ImageDecoderFactory::isFormatSupported(const std::string&){assert(false);return false;}
ImageBlock::ImageBlock(const std::string& p,int16_t w,int16_t h):imagePath(p),width(w),height(h){assert(false);}
bool ImageBlock::imageExists() const {assert(false);return false;}
void ImageBlock::render(GfxRenderer&,int,int){assert(false);}
bool ImageBlock::serialize(FsFile&){assert(false);return false;}
std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile&){assert(false);return {};}

static std::ofstream snapshot;
static unsigned cases = 0;
static void record(const void* p,size_t n) { const uint64_t length=n;snapshot.write(reinterpret_cast<const char*>(&length),8);if(n)snapshot.write(static_cast<const char*>(p),n);assert(snapshot.good()); }
static void measure(bool retained=false) {
  if(!retained) { assert(handles.empty());nextHandle=1; }
  counts={};trace.clear();recording=true;
  Fixture::clockMs=Fixture::lastYield=Fixture::maxYieldGap=0;Fixture::delays=Fixture::yields=Fixture::locks=0;
  ready=true;openFails=closeFails=stickyError=Fixture::lockFails=false;maxRead=errorCall=zeroCall=seekFail=SIZE_MAX;latency=0;
}
static void capture(const std::string& name,const Bytes& cache,std::optional<uint16_t> result) {
  ++cases;
  const auto stamp=Storage.generation();
  record(name.data(),name.size());record(cache.data(),cache.size());record(trace.data(),trace.size());
  const uint64_t summary[]={result?uint64_t(*result):UINT64_MAX,counts.reads,counts.bytes,counts.opens,counts.closes,
    counts.seeks,counts.errors,counts.writes,handles.size(),Fixture::locks,stamp.mount,stamp.mutation,stamp.quiescent};
  record(summary,sizeof(summary));recording=false;
}
static std::string id(size_t n) { char text[32];snprintf(text,sizeof(text),"note%04zu",n);return text; }
constexpr size_t HEADER_SIZE=sizeof(uint8_t)+sizeof(int)+sizeof(float)+sizeof(bool)+sizeof(uint8_t)+3*sizeof(uint16_t)+2*sizeof(bool)+sizeof(uint8_t)+3*sizeof(uint32_t);
template<class T> static T field(const Bytes& data,size_t offset) { assert(offset+sizeof(T)<=data.size());T value;memcpy(&value,data.data()+offset,sizeof(T));return value; }
template<class T> static void patch(Bytes& data,size_t offset,T value) { assert(offset+sizeof(T)<=data.size());memcpy(data.data()+offset,&value,sizeof(T)); }
static std::vector<std::pair<std::string,uint16_t>> decode(const Bytes& data) {
  size_t offset=field<uint32_t>(data,HEADER_SIZE-8);const auto count=field<uint16_t>(data,offset);offset+=2;
  std::vector<std::pair<std::string,uint16_t>> result;
  for(unsigned i=0;i<count;++i){const auto length=field<uint32_t>(data,offset);offset+=4;assert(offset+length+2<=data.size());std::string key(reinterpret_cast<const char*>(data.data()+offset),length);offset+=length;const auto page=field<uint16_t>(data,offset);offset+=2;result.emplace_back(key,page);}
  return result;
}
static Bytes produce(Section& section,const std::string& path,const std::vector<std::string>& keys) {
  recording=false;std::string xml="<?xml version=\"1.0\"?><html xmlns=\"http://www.w3.org/1999/xhtml\"><body>";
  if(!keys.empty())xml+="<p><a href=\"#"+keys.back()+"\">Last note</a></p>";
  for(const auto& key:keys)xml+="<p id=\""+key+"\">A short explanatory note.</p>";
  xml+="</body></html>";files["chapter.xhtml"]=std::make_shared<Bytes>(xml.begin(),xml.end());
  assert(section.createSectionFile(1,1.0f,false,0,400,640,false,false,0));assert(handles.empty());
  assert(section.loadSectionFile(1,1.0f,false,0,400,640,false,false,0));assert(handles.empty());
  if(!keys.empty()){section.currentPage=0;auto first=section.loadPageFromSectionFile();assert(first&&first->footnotes.size()==1&&std::string(first->footnotes[0].href)=="#"+keys.back());assert(handles.empty());}
  const Bytes cache=*files.at(path);const auto map=decode(cache);assert(map.size()==keys.size());
  for(size_t i=0;i<keys.size();++i)assert(map[i].first==keys[i]&&map[i].second<section.pageCount);
  return cache;
}
static void query(Section& section,const std::string& path,const Bytes& cache,const std::string& key,std::optional<uint16_t> expected,const std::string& name) {
  const auto result=section.getPageForAnchor(key);assert(result==expected&&handles.empty()&&*files.at(path)==cache);
  assert(counts.opens==1&&counts.closes==1&&counts.writes==0);capture(name,cache,result);
}
static void workload(size_t count) {
  files.clear();dirs.clear();auto epub=std::make_shared<Epub>("/book.epub","/cache");GfxRenderer renderer;Section section(epub,0,renderer);
  const auto path=epub->getCachePath()+EpubContentCache::sections+"/0.bin";
  std::vector<std::string> keys;for(size_t i=0;i<count;++i)keys.push_back(id(i));
  const auto cache=produce(section,path,keys);const auto map=decode(cache);
  for(size_t index:{size_t(0),count/2,count-1})for(unsigned repeat=0;repeat<2;++repeat){
    measure();query(section,path,cache,id(index),map[index].second,"normal:"+std::to_string(count)+":"+std::to_string(index)+":"+std::to_string(repeat));
    const auto expectedReads=2+3*(index+1);assert(counts.reads==expectedReads&&counts.bytes==6+14*(index+1));
    const auto waits=Fixture::delays+Fixture::yields;
    assert(waits==(EXPECT_OPTIMIZED?expectedReads/32:expectedReads)&&"scheduling bound");
    if(index==count-1&&!repeat)std::cout<<"anchors="<<count<<" reads="<<counts.reads<<" waits="<<waits<<" bytes="<<counts.bytes<<"\n";
  }
  measure();query(section,path,cache,"absent",{},"missing:"+std::to_string(count));assert(counts.reads==2+3*count);
}
static void edgeCases() {
  files.clear();dirs.clear();auto epub=std::make_shared<Epub>("/book.epub","/cache");GfxRenderer renderer;Section section(epub,0,renderer);
  const auto path=epub->getCachePath()+EpubContentCache::sections+"/0.bin";
  // Deliberate duplicate-ID input is a parser-accepted semantic edge fixture.
  // Its duplicates land on different real layout pages, so last-match wins
  // cannot accidentally satisfy the first-match assertion.
  std::vector<std::string> keys;for(unsigned i=0;i<64;++i)keys.push_back(id(i));
  keys[0]="same";keys[1]="Same";keys[60]="same";keys[61]="caf\xc3\xa9";keys[63]="last";
  const auto cache=produce(section,path,keys);const auto map=decode(cache);assert(map[0].second!=map[60].second);
  for(const auto& pair:std::vector<std::pair<std::string,std::optional<uint16_t>>>{{"same",map[0].second},{"Same",map[1].second},{"SAME",{}},{"caf\xc3\xa9",map[61].second},{"",{}}}){
    measure();query(section,path,cache,pair.first,pair.second,"key:"+pair.first);
  }
  for(size_t cap:{size_t(1),size_t(7)}){measure();maxRead=cap;query(section,path,cache,"last",map.back().second,"short-provider:"+std::to_string(cap));}
  for(unsigned ms:{3u,9u})for(bool wrap:{false,true}){
    measure();latency=ms;if(wrap)Fixture::clockMs=Fixture::lastYield=UINT32_MAX-4ull;
    query(section,path,cache,"last",map.back().second,"slow:"+std::to_string(ms)+":"+std::to_string(wrap));
    assert(Fixture::maxYieldGap<=8+ms);
  }
  // Every scalar is fully copied before the injected error is reported. This
  // exercises the old ignored-return contract without indeterminate values.
  for(size_t fault:{size_t(1),size_t(2),size_t(3),size_t(4),size_t(5),size_t(8),size_t(17)})for(bool sticky:{false,true}){
    measure();errorCall=fault;stickyError=sticky;query(section,path,cache,"last",map.back().second,"copy-error:"+std::to_string(fault)+":"+std::to_string(sticky));
    measure();query(section,path,cache,"last",map.back().second,"retry:"+std::to_string(fault)+":"+std::to_string(sticky));
  }
  for(unsigned failure=0;failure<3;++failure){
    measure();if(failure==0)openFails=true;if(failure==1)ready=false;if(failure==2)Fixture::lockFails=true;
    auto result=section.getPageForAnchor("last");assert(!result&&handles.empty()&&!counts.reads);
    Fixture::lockFails=false;capture("admission:"+std::to_string(failure),cache,result);
    measure();query(section,path,cache,"last",map.back().second,"admission-retry:"+std::to_string(failure));
  }
  for(unsigned failure=0;failure<5;++failure){
    auto changed=cache;
    if(failure<3)patch<uint32_t>(changed,HEADER_SIZE-8,failure==0?0:failure==1?changed.size():changed.size()+1);
    // Failed seeks retain a defined cursor. Zero the complete field at that
    // retained position, so rejection/count=0 never decodes uninitialized data.
    if(failure==3)patch<uint32_t>(changed,0,0);
    if(failure==4)patch<uint32_t>(changed,HEADER_SIZE-4,0);
    *files.at(path)=changed;measure();if(failure>=3)seekFail=failure-2;
    query(section,path,changed,"last",{},"offset-seek:"+std::to_string(failure));
    *files.at(path)=cache;measure();query(section,path,cache,"last",map.back().second,"offset-seek-retry:"+std::to_string(failure));
  }
  // A replacement cache on the same Section must be read afresh.
  const auto replacement=produce(section,path,{"last","other"});const auto newMap=decode(replacement);
  measure();query(section,path,replacement,"last",newMap.front().second,"replaced-cache");
  const auto empty=produce(section,path,{});measure();query(section,path,empty,"last",{},"zero-anchors");
  // Long key coverage is an explicit cache-level fixture; its string crosses
  // three unchanged 4096-byte HAL provider chunks. No oversized href is parsed.
  auto longCache=cache;const auto anchorOffset=field<uint32_t>(longCache,HEADER_SIZE-8);longCache.resize(anchorOffset+2);patch<uint16_t>(longCache,anchorOffset,1);
  const std::string longKey(9000,'x');const uint32_t length=longKey.size();const uint16_t page=map.back().second;
  auto append=[&](const void* p,size_t n){auto b=static_cast<const uint8_t*>(p);longCache.insert(longCache.end(),b,b+n);};append(&length,4);append(longKey.data(),longKey.size());append(&page,2);
  *files.at(path)=longCache;measure();query(section,path,longCache,longKey,page,"long-cache-key");
  if(EXPECT_OPTIMIZED)assert(Fixture::yields==2);
  // Ordinary serialization does not opt in merely because another operation did.
  measure();{FsFile f;assert(Storage.openFileForRead("test",path,f));for(unsigned i=0;i<100;++i){uint8_t b=0;serialization::readPod(f,b);}assert(f.close());}
  assert(Fixture::delays==100&&!Fixture::yields);capture("ordinary-control",longCache,{});
  *files.at(path)=cache;
  measure();Storage.markUnavailable();auto absent=section.getPageForAnchor("last");assert(!absent&&handles.empty());assert(Storage.begin());capture("media-mark-recover",cache,absent);
  measure();query(section,path,cache,"last",map.back().second,"media-retry");
  // Keep this last: production destructor cannot retry after it is destroyed.
  // The provider slot remains owned, generation reuse becomes unsafe, and a new
  // lookup must leave the retained slot alone. Teardown below is fixture-only.
  measure();closeFails=true;auto result=section.getPageForAnchor("last");assert(result==map.back().second&&handles.size()==1&&counts.closes==1);capture("destructor-close-failure",cache,result);
  const auto retained=handles.begin()->first;
  measure(true);result=section.getPageForAnchor("last");assert(result==map.back().second&&handles.size()==1&&handles.count(retained));capture("lookup-after-close-failure",cache,result);
  recording=false;assert(providerClose(nullptr,retained,true));
}

static void halEdges() {
  // Use initialized buffers here; no malformed Section scalar is deserialized.
  const std::string path="/hal-input";const Bytes data(9000,0x55);files[path]=std::make_shared<Bytes>(data);
  for(unsigned fault=0;fault<13;++fault){
    measure();FsFile file;assert(Storage.openFileForRead("test",path,file));
    Bytes buffer(9000,0xcc);void* output=buffer.data();size_t size=buffer.size();
    if(fault==0)maxRead=7;
    if(fault==1)zeroCall=1;
    if(fault==2)zeroCall=2;
    if(fault==3)errorCall=1;
    if(fault==4)errorCall=2;
    if(fault==5){latency=5000;maxRead=1;size=10;}
    if(fault==6)size=16u*1024u*1024u+1u;
    if(fault==7)output=nullptr;
    if(fault==8)size=0;
    if(fault==9)ready=false;
    if(fault==10)Fixture::lockFails=true;
    if(fault==11){zeroCall=1;latency=8;}
    if(fault==12){errorCall=1;latency=8;Fixture::clockMs=Fixture::lastYield=UINT32_MAX-4ull;}
    int result;
#if HAL_HAS_COOPERATIVE
    if(EXPECT_OPTIMIZED){HalReadBudget budget([](){return millis();},[](){vTaskDelay(1);});result=file.readCooperatively(output,size,budget);}
    else
#endif
      result=file.read(output,size);
    const int expected=fault==0?9000:fault==1||fault==8||fault==11?0:fault==2||fault==4?4096:fault==5?4:-1;
    assert(result==expected);
    if(fault==5)assert(file.getError()&&counts.reads==4&&counts.bytes==4);
    if(EXPECT_OPTIMIZED&&(fault==11||fault==12))assert(Fixture::yields==1);
    Fixture::lockFails=false;ready=true;const auto error=file.getError();assert(file.close());assert(handles.empty());
    record(buffer.data(),buffer.size());record(&result,sizeof(result));record(&error,sizeof(error));
    capture("initialized-hal:"+std::to_string(fault),data,{});
  }
#if HAL_HAS_COOPERATIVE
  // These primitive checks have no external I/O and do not join parity output.
  for(unsigned scenario=0;scenario<5;++scenario){
    measure();if(scenario==3)Fixture::clockMs=Fixture::lastYield=UINT32_MAX-3ull;
    HalReadBudget budget([](){return millis();},[](){vTaskDelay(1);});
    if(scenario==0){for(unsigned i=0;i<31;++i)budget.afterRead(1);assert(!Fixture::yields);budget.afterRead(1);}
    if(scenario==1){budget.afterRead(4095);assert(!Fixture::yields);budget.afterRead(1);}
    if(scenario==2){Fixture::clockMs=7;budget.checkpoint();assert(!Fixture::yields);Fixture::clockMs=8;budget.checkpoint();}
    if(scenario==3){Fixture::clockMs+=8;budget.checkpoint();}
    if(scenario==4){Fixture::clockMs+=8;budget.checkpoint();budget.checkpoint();}
    assert(Fixture::yields==1);
  }
#endif
  recording=false;
}
int main(int argc,char** argv) {
  assert(argc==2);snapshot.open(argv[1],std::ios::binary);assert(snapshot.good());assert(Storage.bindVolume(&volume.base));
  for(size_t n:{size_t(16),size_t(128),size_t(512),size_t(1024)})workload(n);
  halEdges();edgeCases();snapshot.close();std::cout<<"PASS: "<<cases<<" cases; complete production parser/cache/HAL; exact requests, outputs, errors, cleanup and fresh retries\n";
}
