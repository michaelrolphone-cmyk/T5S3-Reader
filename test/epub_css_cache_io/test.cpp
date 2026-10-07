// Complete production CSS parser, cache codec and volume HAL are linked.
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
#include "Epub/css/CssParser.h"
#include "Epub/ContentCachePaths.h"

using Bytes = std::vector<uint8_t>;
struct Handle { std::shared_ptr<Bytes> bytes; std::string path; size_t pos = 0; bool error = false; };
static std::map<std::string, std::shared_ptr<Bytes>> files;
static std::set<std::string> dirs;
static std::map<uint32_t, Handle> handles;
static uint32_t nextHandle = 1;
struct Counts { uint64_t reads=0, bytes=0, opens=0, closes=0, seeks=0, errors=0, writes=0, writeBytes=0, syncs=0; };
static Counts counts;
static bool recording = false, ready = true, openFails = false, closeFails = false, stickyError = false, syncFails = false;
static size_t maxRead=SIZE_MAX, maxWrite=SIZE_MAX, errorCall=SIZE_MAX, zeroCall=SIZE_MAX, seekFail=SIZE_MAX, dropMediaCall=SIZE_MAX;
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
  if(counts.reads==dropMediaCall)ready=false;
  return got;
}
static size_t providerWrite(void*, uint32_t id, const void* input, size_t want) {
  auto& h=handles.at(id); ++counts.writes; Fixture::clockMs+=latency; assert(want<=4096);
  const size_t got=counts.writes==zeroCall?0:std::min(want,maxWrite);
  if (h.pos+got>h.bytes->size()) h.bytes->resize(h.pos+got);
  if (got) memcpy(h.bytes->data()+h.pos,input,got);
  event(5,{id,h.pos,want,got}); if(recording) raw(input,want);
  h.pos+=got; counts.writeBytes+=got; h.error=(stickyError&&h.error)||counts.writes==errorCall;
  if(counts.writes==dropMediaCall)ready=false;
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
  v.file_sync=[](void*,uint32_t id){++counts.syncs;event(11,{id,!syncFails});return !syncFails;};
  v.dir_rewind=[](void*,uint32_t){assert(false);return false;};
  v.dir_close_checked=[](void*,uint32_t){assert(false);return false;};
  v.mkdir=[](void*,const char* path){dirs.insert(path);return true;};
  v.rename=[](void*,const char* from,const char* to){auto found=files.find(from);if(found==files.end()||files.count(to))return false;files[to]=found->second;files.erase(found);return true;};
  return v;
}();

static const std::string cachePath=std::string("/cache")+EpubContentCache::cssRules;
static std::ofstream snapshot;
static unsigned cases=0;
static std::vector<std::string> selectors;
static void record(const void* p,size_t n) {
  const uint64_t length=n;snapshot.write(reinterpret_cast<const char*>(&length),sizeof(length));
  if(n)snapshot.write(static_cast<const char*>(p),n);
  assert(snapshot.good());
}
static void recordText(const std::string& text) { record(text.data(),text.size()); }
static std::string key(const CssStyle& s) {
  std::ostringstream o;
  o<<int(s.textAlign)<<','<<int(s.fontStyle)<<','<<int(s.fontWeight)<<','<<int(s.textDecoration)<<','<<int(s.display)<<std::hexfloat;
  for(auto l:{s.textIndent,s.marginTop,s.marginBottom,s.marginLeft,s.marginRight,s.paddingTop,s.paddingBottom,
              s.paddingLeft,s.paddingRight,s.imageHeight,s.imageWidth})o<<','<<l.value<<':'<<int(l.unit);
  o<<'|'<<s.defined.textAlign<<s.defined.fontStyle<<s.defined.fontWeight<<s.defined.textDecoration
   <<s.defined.textIndent<<s.defined.marginTop<<s.defined.marginBottom<<s.defined.marginLeft<<s.defined.marginRight
   <<s.defined.paddingTop<<s.defined.paddingBottom<<s.defined.paddingLeft<<s.defined.paddingRight
   <<s.defined.imageHeight<<s.defined.imageWidth<<s.defined.display;
  return o.str();
}
static void measure(bool retained=false) {
  if(!retained){assert(handles.empty());nextHandle=1;}
  counts={};trace.clear();recording=true;
  Fixture::clockMs=Fixture::lastYield=Fixture::maxYieldGap=0;
  Fixture::delays=Fixture::yields=Fixture::locks=0;
  ready=true;openFails=closeFails=stickyError=syncFails=Fixture::lockFails=false;
  maxRead=maxWrite=errorCall=zeroCall=seekFail=dropMediaCall=SIZE_MAX;latency=0;
}
static uint64_t waits() { return Fixture::delays+Fixture::yields; }
static void capture(const std::string& name,const CssParser* parser,bool result) {
  ++cases;const auto stamp=Storage.generation();recordText(name);record(trace.data(),trace.size());
  const uint64_t summary[]={result,parser?parser->ruleCount():UINT64_MAX,counts.reads,counts.bytes,counts.opens,
    counts.closes,counts.seeks,counts.errors,counts.writes,counts.writeBytes,counts.syncs,handles.size(),
    Fixture::locks,stamp.mount,stamp.mutation,stamp.quiescent};record(summary,sizeof(summary));
  if(parser)for(const auto& selector:selectors)recordText(key(parser->resolveStyle("p",selector)));
  for(const auto& entry:files){recordText(entry.first);record(entry.second->data(),entry.second->size());}
  for(const auto& entry:handles){recordText(entry.second.path);const uint64_t state[]={entry.first,entry.second.pos,entry.second.error};record(state,sizeof(state));}
  recording=false;
}
static std::string cls(size_t n) { char name[32];snprintf(name,sizeof(name),"s%04zu",n);return name; }
static std::string ordinaryCss(size_t count) {
  selectors.clear();std::string css;
  for(size_t i=0;i<count;++i){selectors.push_back(cls(i));css+='.'+cls(i)+" { margin-left: "+std::to_string(i%7+1)+
    "px; font-weight: "+(i%2?"normal":"bold")+"; display: "+(i%31?"block":"none")+"; }\n";}
  assert(css.size()<128*1024);return css;
}
static void parse(CssParser& parser,const std::string& css,size_t expected) {
  recording=false;files["styles.css"]=std::make_shared<Bytes>(css.begin(),css.end());
  FsFile source;assert(Storage.openFileForRead("test","styles.css",source));
  assert(parser.loadFromStream(source)&&source.close());assert(parser.ruleCount()==expected&&handles.empty());
}
static std::vector<std::string> styles(const CssParser& parser) {
  std::vector<std::string> result;for(const auto& selector:selectors)result.push_back(key(parser.resolveStyle("p",selector)));return result;
}
static Bytes produce(CssParser& parser,const std::string& css,size_t count) {
  parse(parser,css,count);assert(parser.saveToCache()&&handles.empty());return *files.at(cachePath);
}
static void restore(const Bytes& data) { files[cachePath]=std::make_shared<Bytes>(data); }
static void loadGood(CssParser& parser,const Bytes& data,const std::vector<std::string>& expected,const std::string& name) {
  const bool result=parser.loadFromCache();assert(result&&parser.ruleCount()==expected.size());
  assert(styles(parser)==expected&&handles.empty()&&*files.at(cachePath)==data);
  assert(counts.opens==1&&counts.closes==1&&!counts.writes);capture(name,&parser,result);
}
template<class T> static void patch(Bytes& data,size_t offset,T value) {
  assert(offset+sizeof(T)<=data.size());memcpy(data.data()+offset,&value,sizeof(value));
}
static void normal(size_t count) {
  files.clear();dirs.clear();CssParser parser("/cache");const auto css=ordinaryCss(count);parse(parser,css,count);
  const auto expected=styles(parser);
  for(size_t i=0;i<count;++i){const auto style=parser.resolveStyle("p",cls(i));
    assert(style.marginLeft.value==float(i%7+1)&&style.marginLeft.unit==CssUnit::Pixels);
    assert(style.hasMarginLeft()&&style.hasFontWeight()&&style.hasDisplay());}
  measure();assert(parser.saveToCache()&&handles.empty());const auto saved=*files.at(cachePath);
  const auto ops=30*count+2;
  assert(counts.writes==ops&&counts.writeBytes==saved.size()&&counts.opens==1&&counts.closes==1);
  // Complete HAL mkdir(/cache) has one existing cooperative wait of its own.
  if(!NEGATIVE_READ_ONLY)assert(waits()==(EXPECT_OPTIMIZED?ops/32+1:ops+1)&&"write scheduling bound");
  const auto writeWaits=waits();capture("normal-save:"+std::to_string(count),&parser,true);
  for(unsigned repeat=0;repeat<3;++repeat){
    parser.clear();measure();loadGood(parser,saved,expected,"normal-load:"+std::to_string(count)+":"+std::to_string(repeat));
    assert(counts.reads==ops&&counts.bytes==saved.size());
    assert(waits()==(EXPECT_OPTIMIZED?ops/32:ops)&&"read scheduling bound");
    if(!repeat)std::cout<<"rules="<<count<<" css_bytes="<<css.size()<<" cache_bytes="<<saved.size()<<" reads="<<counts.reads
      <<" read_waits="<<waits()<<" writes="<<ops<<" write_waits="<<writeWaits<<"\n";
  }
}
static void fullStyles() {
  files.clear();dirs.clear();selectors.clear();CssParser parser("/cache");std::string css;
  const char* units[]={"px","em","rem","pt","%"};const char* align[]={"justify","left","center","right","inherit"};
  const char* props[]={"text-indent","margin-top","margin-bottom","margin-left","margin-right","padding-top",
    "padding-bottom","padding-left","padding-right","height","width"};
  std::vector<std::string> expected;
  for(unsigned i=0;i<10;++i){std::string declaration="text-align:"+std::string(align[i%5])+";font-style:"+(i%2?"italic":"normal")+
    ";font-weight:"+(i%2?"bold":"normal")+";text-decoration:"+(i%2?"underline":"none")+";display:"+(i%2?"none":"block")+";";
    for(unsigned j=0;j<11;++j)declaration+=std::string(props[j])+":"+std::to_string(j+1)+".25"+units[(i+j)%5]+";";
    selectors.push_back(cls(i));css+='.'+cls(i)+"{"+declaration+"}\n";expected.push_back(key(CssParser::parseInlineStyle(declaration)));
  }
  const auto saved=produce(parser,css,10);assert(styles(parser)==expected);
  std::set<int> seenUnits,seenAlign,seenFontStyle,seenWeight,seenDecoration,seenDisplay;
  for(const auto& selector:selectors){const auto s=parser.resolveStyle("p",selector);
    assert(s.hasTextAlign()&&s.hasFontStyle()&&s.hasFontWeight()&&s.hasTextDecoration()&&s.hasTextIndent()&&s.hasMarginTop()&&s.hasMarginBottom()
      &&s.hasMarginLeft()&&s.hasMarginRight()&&s.hasPaddingTop()&&s.hasPaddingBottom()&&s.hasPaddingLeft()&&s.hasPaddingRight()
      &&s.hasImageHeight()&&s.hasImageWidth()&&s.hasDisplay());
    seenAlign.insert(int(s.textAlign));seenFontStyle.insert(int(s.fontStyle));seenWeight.insert(int(s.fontWeight));
    seenDecoration.insert(int(s.textDecoration));seenDisplay.insert(int(s.display));
    unsigned j=0;for(auto l:{s.textIndent,s.marginTop,s.marginBottom,s.marginLeft,s.marginRight,s.paddingTop,s.paddingBottom,
      s.paddingLeft,s.paddingRight,s.imageHeight,s.imageWidth}){assert(l.value==float(++j)+0.25f);seenUnits.insert(int(l.unit));}
  }
  assert(seenUnits.size()==5&&seenAlign.size()==4&&seenFontStyle.size()==2&&seenWeight.size()==2&&seenDecoration.size()==2&&seenDisplay.size()==2);
  for(unsigned repeat=0;repeat<2;++repeat){measure();loadGood(parser,saved,expected,"all-fields:"+std::to_string(repeat));}
  // The healthy workload above has mixed defined/undefined flags; these ten
  // styles exercise every length and all representable parser-selected enums.
}
static void readEdges() {
  files.clear();dirs.clear();CssParser parser("/cache");const auto saved=produce(parser,ordinaryCss(16),16);const auto expected=styles(parser);
  for(size_t cap:{size_t(1),size_t(2),size_t(7)}){measure();maxRead=cap;loadGood(parser,saved,expected,"read-short:"+std::to_string(cap));}
  for(unsigned ms:{3u,9u})for(bool wrap:{false,true}){
    measure();latency=ms;if(wrap)Fixture::clockMs=Fixture::lastYield=UINT32_MAX-4ull;
    loadGood(parser,saved,expected,"read-time:"+std::to_string(ms)+":"+std::to_string(wrap));assert(Fixture::maxYieldGap<=8+ms);
  }
  for(size_t call:{size_t(1),size_t(5)}){
    measure();dropMediaCall=call;assert(!parser.loadFromCache()&&parser.empty()&&handles.empty());
    capture("read-media-loss:"+std::to_string(call),&parser,false);
    measure();loadGood(parser,saved,expected,"read-media-retry:"+std::to_string(call));
  }
  // Each scalar field and selector read fails in turn. CSS validates all read
  // counts, clears incomplete maps, and removes version-read failures as before.
  for(size_t fault=1;fault<=33;++fault)for(unsigned kind=0;kind<3;++kind){
    restore(saved);measure();if(kind==0)zeroCall=fault;else{errorCall=fault;stickyError=kind==2;}
    const bool result=parser.loadFromCache();assert(!result&&parser.empty()&&handles.empty());
    assert((files.count(cachePath)==0)==(fault==1));capture("read-fault:"+std::to_string(fault)+":"+std::to_string(kind),&parser,result);
    restore(saved);measure();loadGood(parser,saved,expected,"read-fault-retry:"+std::to_string(fault)+":"+std::to_string(kind));
  }
  for(unsigned fault=0;fault<4;++fault){
    measure();if(fault==0)openFails=true;if(fault==1)ready=false;if(fault==2)Fixture::lockFails=true;if(fault==3)files.erase(cachePath);
    const bool result=parser.loadFromCache();assert(!result&&parser.ruleCount()==16&&handles.empty()&&!counts.reads);
    Fixture::lockFails=false;capture("read-admission:"+std::to_string(fault),&parser,result);
    restore(saved);measure();loadGood(parser,saved,expected,"read-admission-retry:"+std::to_string(fault));
  }
  // Exhaustive byte truncation of the header and first complete rule, plus a
  // truncation after a previously accepted rule and at the final style field.
  std::set<size_t> cuts;for(size_t n=0;n<74;++n)cuts.insert(n);cuts.insert(saved.size()-1);cuts.insert(saved.size()-2);
  for(size_t cut:cuts){auto truncated=saved;truncated.resize(cut);restore(truncated);measure();
    const bool result=parser.loadFromCache();assert(!result&&parser.empty()&&handles.empty());
    assert((files.count(cachePath)==0)==(cut==0));capture("truncated:"+std::to_string(cut),&parser,result);
    restore(saved);measure();loadGood(parser,saved,expected,"truncated-retry:"+std::to_string(cut));
  }
  for(unsigned scenario=0;scenario<8;++scenario){auto changed=saved;
    if(scenario==0)changed[0]=0;
    if(scenario==1)patch<uint16_t>(changed,1,1501);
    if(scenario==2)patch<uint16_t>(changed,3,0);
    if(scenario==3)patch<uint16_t>(changed,3,257);
    if(scenario==4)patch<uint16_t>(changed,1,0);
    if(scenario==5)patch<uint16_t>(changed,1,1);
    if(scenario==6)changed.push_back(0xff); // Existing trailing-byte acceptance.
    if(scenario==7){changed[11]=0xfe;changed[71]|=1;} // Existing unvalidated uint8 enum acceptance.
    restore(changed);measure();const bool result=parser.loadFromCache();
    assert(result==(scenario>=4)&&handles.empty());
    assert(parser.ruleCount()==(scenario<5?0:scenario==5?1:16));
    assert((files.count(cachePath)==0)==(scenario==0));capture("format:"+std::to_string(scenario),&parser,result);
    restore(saved);measure();loadGood(parser,saved,expected,"format-retry:"+std::to_string(scenario));
  }
  // Exact maximum selector length is a cache-format boundary fixture; the
  // ordinary workloads are entirely production-parser-generated above.
  auto maximum=saved;patch<uint16_t>(maximum,1,1);maximum.resize(3+70);patch<uint16_t>(maximum,3,256);
  maximum.erase(maximum.begin()+5,maximum.begin()+11);maximum.insert(maximum.begin()+5,255,'x');maximum.insert(maximum.begin()+5,'.');
  restore(maximum);measure();assert(parser.loadFromCache()&&parser.ruleCount()==1&&handles.empty());
  assert(parser.resolveStyle("p",std::string(255,'x')).hasMarginLeft());capture("maximum-selector",&parser,true);
  restore(saved);measure();loadGood(parser,saved,expected,"maximum-selector-retry");
  measure();Storage.markUnavailable();const bool absent=parser.loadFromCache();assert(!absent&&parser.ruleCount()==16&&handles.empty());
  assert(Storage.begin());capture("media-unavailable",&parser,absent);
  measure();loadGood(parser,saved,expected,"media-recovered");
  CssParser noCache("");measure();assert(!noCache.saveToCache()&&!noCache.loadFromCache()&&!counts.opens);capture("empty-cache-path",&noCache,false);
  CssParser empty("/cache");measure();assert(empty.saveToCache()&&handles.empty());capture("empty-rules-save",&empty,true);
  measure();assert(empty.loadFromCache()&&empty.empty()&&handles.empty());capture("empty-rules-load",&empty,true);
}
static void writeEdges() {
  files.clear();dirs.clear();CssParser parser("/cache");const auto saved=produce(parser,ordinaryCss(16),16);const auto expected=styles(parser);
  for(unsigned ms:{3u,9u})for(bool wrap:{false,true}){
    measure();latency=ms;if(wrap)Fixture::clockMs=Fixture::lastYield=UINT32_MAX-4ull;
    assert(parser.saveToCache()&&handles.empty()&&*files.at(cachePath)==saved);
    assert(Fixture::maxYieldGap<=8+ms);capture("write-time:"+std::to_string(ms)+":"+std::to_string(wrap),&parser,true);
  }
  for(size_t call:{size_t(1),size_t(5)}){
    measure();dropMediaCall=call;assert(parser.saveToCache()&&handles.empty()&&files.at(cachePath)->size()<saved.size());
    capture("write-media-loss-ignored:"+std::to_string(call),&parser,true);
    measure();assert(parser.saveToCache()&&handles.empty()&&*files.at(cachePath)==saved);capture("write-media-retry:"+std::to_string(call),&parser,true);
  }
  // Existing saveToCache ignores every scalar write result. Preserve and expose
  // that contract, including shortened bytes and success after provider errors.
  for(size_t fault=1;fault<=33;++fault)for(unsigned kind=0;kind<3;++kind){
    restore(saved);measure();if(kind==0)zeroCall=fault;else{errorCall=fault;stickyError=kind==2;}
    assert(parser.saveToCache()&&styles(parser)==expected&&handles.empty());assert(counts.writes==482);
    assert(files.at(cachePath)->size()<saved.size() || kind!=0);
    capture("write-fault-ignored:"+std::to_string(fault)+":"+std::to_string(kind),&parser,true);
    measure();assert(parser.saveToCache()&&handles.empty()&&*files.at(cachePath)==saved);
    capture("write-fault-retry:"+std::to_string(fault)+":"+std::to_string(kind),&parser,true);
  }
  for(size_t cap:{size_t(1),size_t(2)}){
    measure();maxWrite=cap;assert(parser.saveToCache()&&handles.empty()&&counts.writes==482&&*files.at(cachePath)!=saved);
    capture("write-short-ignored:"+std::to_string(cap),&parser,true);
    measure();assert(parser.saveToCache()&&handles.empty()&&*files.at(cachePath)==saved);capture("write-short-retry:"+std::to_string(cap),&parser,true);
  }
  for(unsigned fault=0;fault<3;++fault){
    measure();if(fault==0)openFails=true;if(fault==1)ready=false;if(fault==2)Fixture::lockFails=true;
    assert(!parser.saveToCache()&&handles.empty()&&!counts.writes&&*files.at(cachePath)==saved&&styles(parser)==expected);
    Fixture::lockFails=false;capture("write-admission:"+std::to_string(fault),&parser,false);
    measure();assert(parser.saveToCache()&&handles.empty()&&*files.at(cachePath)==saved);capture("write-admission-retry:"+std::to_string(fault),&parser,true);
  }
  // Ordinary callers keep their one wait per healthy transfer after CSS opt-in.
  measure();{
    FsFile file;assert(Storage.openFileForRead("test",cachePath,file));
    for(unsigned i=0;i<100;++i){uint8_t byte=0;assert(file.read(&byte,1)==1);}assert(file.close());
  }assert(waits()==100);capture("ordinary-read-control",&parser,true);
  measure();{
    auto file=Storage.open("/ordinary-output",O_RDWR|O_CREAT|O_TRUNC);assert(file);
    for(unsigned i=0;i<100;++i)assert(file.write(static_cast<uint8_t>(i))==1);
    assert(file.close());
  }assert(waits()==100);capture("ordinary-write-control",&parser,true);
}
static int readHal(FsFile& file,void* data,size_t size) {
  if(EXPECT_OPTIMIZED){HalReadBudget budget([](){return millis();},[](){delay(1);});return file.readCooperatively(data,size,budget);}
  return file.read(data,size);
}
static size_t writeHal(FsFile& file,const void* data,size_t size) {
#if HAL_HAS_WRITE_BUDGET
  if(EXPECT_OPTIMIZED){HalWriteBudget budget([](){return millis();},[](){delay(1);});return file.writeCooperatively(data,size,budget);}
#endif
  return file.write(data,size);
}
static void halEdges() {
  files.clear();dirs.clear();const std::string path="/hal-input";const Bytes data(9000,0x55);files[path]=std::make_shared<Bytes>(data);
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
    const int result=readHal(file,output,size);
    const int expected=fault==0?9000:fault==1||fault==8||fault==11?0:fault==2||fault==4?4096:fault==5?4:-1;
    assert(result==expected);
    if(fault==5)assert(file.getError()&&counts.reads==4&&counts.bytes==4);
    if(EXPECT_OPTIMIZED&&(fault==11||fault==12))assert(waits()==1);
    Fixture::lockFails=false;ready=true;const auto error=file.getError();assert(file.close()&&handles.empty());
    record(buffer.data(),buffer.size());record(&result,sizeof(result));record(&error,sizeof(error));
    capture("hal-read:"+std::to_string(fault),nullptr,true);
  }
  for(unsigned fault=0;fault<17;++fault){
    measure();auto file=Storage.open("/hal-output",O_RDWR|O_CREAT|O_TRUNC|((fault==11||fault==12)?O_SYNC:0));assert(file);
    const void* input=data.data();size_t size=data.size();
    if(fault==0)maxWrite=7;
    if(fault==1)zeroCall=1;
    if(fault==2)zeroCall=2;
    if(fault==3)errorCall=1;
    if(fault==4)errorCall=2;
    if(fault==5)latency=10000;
    if(fault==6)size=16u*1024u*1024u+1u;
    if(fault==7)input=nullptr;
    if(fault==8)size=0;
    if(fault==9)ready=false;
    if(fault==10)Fixture::lockFails=true;
    if(fault==11)syncFails=true;
    if(fault==13){zeroCall=1;latency=8;}
    if(fault==14){errorCall=1;latency=8;Fixture::clockMs=Fixture::lastYield=UINT32_MAX-4ull;}
    if(fault==15){assert(file.close());}
    if(fault==16){assert(file.close());file=Storage.open("/hal-output",O_RDONLY);assert(file);}
    const auto result=writeHal(file,input,size);
    const size_t expected=fault==0?7:fault==2||fault==3||fault==11||fault==14?4096:fault==4||fault==5?8192:fault==12?9000:0;
    assert(result==expected);
    if(fault==5)assert(file.getError()&&counts.writes==2&&counts.writeBytes==8192);
    if(EXPECT_OPTIMIZED&&(fault==13||fault==14))assert(waits()==1);
    Fixture::lockFails=false;ready=true;const auto error=file.getError();assert(file.close()&&handles.empty());
    record(&result,sizeof(result));record(&error,sizeof(error));capture("hal-write:"+std::to_string(fault),nullptr,true);
  }
  // A failed sync and a subsequent retry retain the existing generation/error contract.
  measure();{auto file=Storage.open("/hal-output",O_RDWR);assert(file);syncFails=true;file.flush();assert(file.getError());
    syncFails=false;file.flush();assert(file.getError()&&file.close());}capture("flush-failure-retry",nullptr,true);
#if HAL_HAS_WRITE_BUDGET
  // Primitive thresholds are checked without adding variant-specific I/O to the snapshot.
  for(bool write:{false,true})for(unsigned scenario=0;scenario<6;++scenario){
    measure();if(scenario==3)Fixture::clockMs=Fixture::lastYield=UINT32_MAX-3ull;
    HalReadBudget read([](){return millis();},[](){delay(1);});HalWriteBudget output([](){return millis();},[](){delay(1);});
    const auto after=[&](size_t n){if(write)output.afterWrite(n);else read.afterRead(n);};
    const auto check=[&](){if(write)output.checkpoint();else read.checkpoint();};
    if(scenario==0){for(unsigned i=0;i<31;++i)after(1);assert(!waits());after(1);}
    if(scenario==1){after(4095);assert(!waits());after(1);}
    if(scenario==2){Fixture::clockMs=7;check();assert(!waits());Fixture::clockMs=8;check();}
    if(scenario==3){Fixture::clockMs+=8;check();}
    if(scenario==4){Fixture::clockMs+=8;check();check();}
    if(scenario==5){Fixture::clockMs+=8;check();after(4095);assert(waits()==1);after(1);assert(waits()==2);continue;}
    assert(waits()==1);
  }
#endif
  recording=false;
}
static void closeEdges() {
  files.clear();dirs.clear();CssParser parser("/cache");auto saved=produce(parser,ordinaryCss(16),16);const auto expected=styles(parser);
  // A recoverable explicit-close retry exercises the real retained Impl slot.
  measure();{auto file=Storage.open(cachePath.c_str());assert(file);closeFails=true;assert(!file.close()&&handles.size()==1);
    closeFails=false;assert(file.close()&&handles.empty());}capture("explicit-close-retry",&parser,true);
  // Destruction cannot retry a failed close. The provider slot remains live and
  // generation reuse is poisoned. Subsequent CSS calls must leave it untouched.
  for(bool write:{false,true}){
    if(write){recording=false;assert(parser.saveToCache()&&handles.empty());saved=*files.at(cachePath);}
    measure();closeFails=true;const bool result=write?parser.saveToCache():parser.loadFromCache();
    assert(result&&handles.size()==1&&counts.closes==1&&*files.at(cachePath)==saved&&styles(parser)==expected);
    assert(!Storage.generation().quiescent);capture(write?"writer-close-failure-ignored":"reader-close-failure",&parser,result);
    const auto retained=handles.begin()->first;
    measure(true);const bool retry=write?parser.saveToCache():parser.loadFromCache();
    assert(retry&&handles.size()==1&&handles.count(retained)&&*files.at(cachePath)==saved);
    capture(write?"write-after-retained-close":"read-after-retained-close",&parser,retry);
    recording=false;assert(providerClose(nullptr,retained,true)); // Explicit provider-only teardown.
  }
}
int main(int argc,char** argv) {
  assert(argc==2);snapshot.open(argv[1],std::ios::binary);assert(snapshot.good());assert(Storage.bindVolume(&volume.base));
  for(size_t n:{size_t(16),size_t(128),size_t(512),size_t(1500)})normal(n);
  fullStyles();readEdges();writeEdges();halEdges();closeEdges();snapshot.close();
  std::cout<<"PASS: "<<cases<<" cases; complete production CSS/HAL, exact requests/data/results/generation/cleanup and retries\n";
}
