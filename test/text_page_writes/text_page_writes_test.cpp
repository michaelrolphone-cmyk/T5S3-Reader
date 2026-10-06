#include <cassert>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstdlib>
#include <GfxRenderer.h>
#include <Serialization.h>
#include <Logging.h>
#include <freertos/task.h>
#include "Page.h"
int HalFile::read(void* p,size_t n){auto& f=*impl;assert(f.pos<=f.data.size());n=std::min(n,f.data.size()-f.pos);if(n)memcpy(p,f.data.data()+f.pos,n);f.pos+=n;return n;}
static size_t providerWrite(void* ctx,unsigned,const void* bytes,size_t n){
 auto& f=*static_cast<FileImpl*>(ctx);++counts.writes;clockMs+=providerMillis;assert(n<=4096);
 const size_t got=counts.writes==zeroCall?0:counts.writes==shortCall?n/2:n;
 trace.insert(trace.end(),{f.pos,n,got});
 if(f.pos+got>f.data.size()) { f.data.resize(f.pos+got); }
 if(got) { memcpy(f.data.data()+f.pos,bytes,got); }
 f.pos+=got;counts.bytes+=got;providerError=counts.writes==errorCall;return got;
}
static bool syncFile(void*,unsigned){++counts.syncs;return counts.syncs!=syncFault;}
static bool handleError(void*,unsigned,bool){++counts.errors;return providerError;}
Volume v{nullptr,providerWrite};Extended e{syncFile,handleError};Volume* volume=&v;Extended* extended=&e;
ImageBlock::ImageBlock(const std::string& p,int16_t w,int16_t h):imagePath(p),width(w),height(h){}
void ImageBlock::render(GfxRenderer& renderer,int x,int y){renderer.commands.push_back("image:"+imagePath+":"+std::to_string(x)+":"+std::to_string(y)+":"+std::to_string(width)+":"+std::to_string(height));}
#include "ImageSerialization.inc"
class Section {public:FsFile file;uint16_t pageCount=0;uint32_t onPageComplete(std::unique_ptr<Page>);};
#include "SectionWrite.inc"
static uint64_t hash(const void* ptr,size_t n){const auto* p=static_cast<const uint8_t*>(ptr);uint64_t h=1469598103934665603ull;while(n--)h=(h^*p++)*1099511628211ull;return h;}
static void reset(){counts={};trace.clear();clockMs=lastYield=maxYieldGap=0;providerMillis=0;shortCall=zeroCall=errorCall=syncFault=SIZE_MAX;ready=true;lockFails=closeFails=providerError=false;}
static void bind(FsFile& file){volume->context=file.impl.get();}
static Page pageFor(unsigned lines,unsigned words,bool focus,bool varied=false,bool mixed=false){
 Page page;
 for(unsigned l=0;l<lines;++l){
  std::vector<std::string> ws;std::vector<int16_t> xs;std::vector<EpdFontFamily::Style> styles;std::vector<uint8_t> bs;std::vector<uint16_t> sx;
  for(unsigned w=0;w<words;++w){ws.push_back(varied?(w%4==0?"":w%4==1?"é中":w%4==2?"\xe2\x80\x83underlined":std::string(64,'a')):"words");xs.push_back(int(w)*40-20);styles.push_back(static_cast<EpdFontFamily::Style>(w%8));if(focus){bs.push_back(w%4==0?0:w%4==1?2:w%4==2?39:200);sx.push_back(14+w);}}
  BlockStyle st;st.alignment=CssTextAlign::Justify;st.textAlignDefined=true;st.marginTop=3;st.marginBottom=4;st.marginLeft=5;st.marginRight=6;st.paddingTop=7;st.paddingBottom=8;st.paddingLeft=9;st.paddingRight=10;st.textIndent=11;st.textIndentDefined=true;
  auto block=std::make_shared<TextBlock>(ws,xs,styles,bs,sx,st);page.elements.push_back(std::make_shared<PageLine>(block,5,l*20));
 }
 if(mixed){page.elements.insert(page.elements.begin()+lines/2,std::make_shared<PageImage>(std::make_shared<ImageBlock>("/OPS/Images/é.png",41,33),-3,79));page.addFootnote("1","Text/note.xhtml#n1");page.addFootnote("99","../notes.xhtml#end");}
 return page;
}
static void snapshot(const std::string& name,const FsFile& file,size_t result,const std::string& rendering=""){
 if(const char* path=std::getenv("WRITE_SNAPSHOT_PATH")){
  std::ofstream out(path,std::ios::binary|std::ios::app);assert(out);
  const auto record=[&](const void* data,size_t n){out.write(reinterpret_cast<const char*>(&n),sizeof(n));if(n)out.write(static_cast<const char*>(data),n);};
  record(name.data(),name.size());record(trace.data(),trace.size()*sizeof(uint64_t));record(file.impl->data.data(),file.impl->data.size());record(rendering.data(),rendering.size());assert(out.good());
 }

 std::cout<<name<<" result="<<result<<" writes="<<counts.writes<<" bytes="<<counts.bytes<<" locks="<<counts.locks<<" mutations="<<counts.mutations<<" syncs="<<counts.syncs<<" errors="<<counts.errors<<" error="<<file.impl->error<<" position="<<file.impl->pos<<" closes="<<counts.closes<<" trace="<<std::hex<<hash(trace.data(),trace.size()*sizeof(uint64_t))<<" payload="<<hash(file.impl->data.data(),file.impl->data.size())<<" render="<<hash(rendering.data(),rendering.size())<<std::dec<<"\n";
 std::cerr<<"cost "<<name<<" ordinary="<<counts.delays<<" budget_and_section="<<counts.yields<<" waits="<<counts.delays+counts.yields<<"\n";
}
static void healthy(unsigned lines,unsigned words,bool focus,unsigned pages=1,bool varied=false,bool mixed=false,unsigned slow=0,bool sync=false){
 reset();providerMillis=slow;Section section;section.file.impl->data.resize(32);section.file.impl->pos=32;section.file.impl->syncWrites=sync;bind(section.file);std::vector<size_t> offsets;
 for(unsigned p=0;p<pages;++p){const size_t off=section.onPageComplete(std::make_unique<Page>(pageFor(lines,words,focus,varied,mixed)));assert(off>=32);offsets.push_back(off);}
 assert(section.pageCount==pages);assert(counts.syncs==(sync?counts.writes:0));
 if(!varied&&!mixed&&!slow){assert(counts.writes==pages*(2+lines*(17+(focus?6:4)*words)));assert(counts.mutations==counts.writes);assert(counts.locks==counts.writes);
#ifdef TEST_BASELINE
 assert(counts.delays==counts.writes&&counts.yields==pages/16);
#else
 assert(counts.delays==pages*(2+3*lines));assert(counts.yields==pages*lines*((14+(focus?6:4)*words)/32)+pages/16);
#endif
 }
 if(slow)assert(maxYieldGap<=8+slow);
 // Full production decoding/render-command parity and byte-exact reserialization.
 const auto before=counts;const auto oldTrace=trace;const auto bytes=section.file.impl->data;std::string render;
 for(unsigned p=0;p<pages;++p){section.file.seek(offsets[p]);auto decoded=Page::deserialize(section.file);assert(decoded);auto expected=pageFor(lines,words,focus,varied,mixed);GfxRenderer er,dr;expected.render(er,42,13,-9);decoded->render(dr,42,13,-9);assert(er.commands==dr.commands);for(const auto& c:dr.commands)render+=c+'\n';FsFile reserialized;bind(reserialized);assert(decoded->serialize(reserialized));const size_t end=p+1<pages?offsets[p+1]:bytes.size();assert(reserialized.impl->data==std::vector<uint8_t>(bytes.begin()+offsets[p],bytes.begin()+end));}
 counts=before;trace=oldTrace;section.file.impl->pos=bytes.size();assert(section.file.close());
 snapshot("healthy:"+std::to_string(lines)+":"+std::to_string(words)+":"+std::to_string(focus)+":"+std::to_string(pages)+":"+std::to_string(varied)+":"+std::to_string(mixed)+":"+std::to_string(slow)+":"+std::to_string(sync),section.file,section.pageCount,render);
}
static void failures(){
 // Every scalar remains a write; inject deterministic provider faults by call number.
 // Do not deserialize damaged legacy bytes: existing unchecked writes are unchanged.
 for(unsigned kind=0;kind<4;++kind)for(size_t call:{1u,2u,4u,9u,17u,32u,64u,173u,233u,235u}){
  reset();Section section;section.file.impl->pos=32;section.file.impl->data.resize(32);bind(section.file);
  if(kind==0) { shortCall=call; }if(kind==1) { zeroCall=call; }if(kind==2) { errorCall=call; }if(kind==3){section.file.impl->syncWrites=true;syncFault=call;}
  auto page=pageFor(3,10,true,true,true);const auto result=section.onPageComplete(std::make_unique<Page>(std::move(page)));assert(section.file.impl->handle);
  // Close failure keeps the handle available to the caller's next close attempt.
  closeFails=true;assert(!section.file.close()&&section.file.impl->handle);closeFails=false;assert(section.file.close()&&!section.file.impl->handle);
  snapshot("fault:"+std::to_string(kind)+":"+std::to_string(call),section.file,result);
  reset();Section retry;retry.file.impl->pos=32;retry.file.impl->data.resize(32);bind(retry.file);assert(retry.onPageComplete(std::make_unique<Page>(pageFor(3,10,true,true,true)))==32);assert(retry.pageCount==1&&!retry.file.impl->error);assert(retry.file.close());
 }
 for(unsigned invalid=0;invalid<4;++invalid){
  reset();Section section;bind(section.file);section.file.impl->pos=32;section.file.impl->data.resize(32);
  std::vector<std::string> words{"one","two"};std::vector<int16_t> xs{0,10};std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR,EpdFontFamily::BOLD};std::vector<uint8_t> bs{1,2};std::vector<uint16_t> sx{4,8};
  if(invalid==0) { xs.pop_back(); }if(invalid==1) { styles.pop_back(); }if(invalid==2) { bs.pop_back(); }if(invalid==3) { sx.pop_back(); }
  auto page=std::make_unique<Page>();page->elements.push_back(std::make_shared<PageLine>(std::make_shared<TextBlock>(words,xs,styles,bs,sx),0,0));assert(section.onPageComplete(std::move(page))==0);assert(!section.pageCount&&counts.writes==4);snapshot("invalid-vectors:"+std::to_string(invalid),section.file,0);
 }
 reset();Section missing;missing.file.impl->handle=0;bind(missing.file);assert(!missing.onPageComplete(std::make_unique<Page>(pageFor(1,1,false)))&&!counts.writes&&!counts.mutations);
 // Checked footnote write failure must keep the same caller failure/count boundary.
 reset();Section fn;bind(fn.file);fn.file.impl->pos=32;fn.file.impl->data.resize(32);auto p=std::make_unique<Page>();p->addFootnote("1","x");zeroCall=3;assert(!fn.onPageComplete(std::move(p))&&!fn.pageCount&&counts.writes==3);snapshot("footnote-failure",fn.file,0);
}
static void largeString(){
 reset();Page page;std::vector<std::string> words{std::string(9000,'x')};std::vector<int16_t> xs{0};std::vector<EpdFontFamily::Style> styles{EpdFontFamily::ITALIC};page.elements.push_back(std::make_shared<PageLine>(std::make_shared<TextBlock>(words,xs,styles),0,0));FsFile f;bind(f);assert(page.serialize(f));snapshot("long-string",f,1);
#ifndef TEST_BASELINE
 assert(counts.yields==2&&counts.delays==5);
#endif
 auto bytes=f.impl->data;f.seek(0);auto decoded=Page::deserialize(f);assert(decoded);FsFile re;bind(re);assert(decoded->serialize(re)&&re.impl->data==bytes);
}
#ifndef TEST_ORIGINAL_HAL
static size_t writePath(FsFile& f,const void* ptr,size_t size,bool cooperative,HalWriteBudget& b){return cooperative?f.writeCooperatively(ptr,size,b):f.write(ptr,size);}
static void halBounds(){
 for(unsigned kind=0;kind<12;++kind){
  std::vector<uint64_t> originalTrace;std::vector<uint8_t> originalData;size_t originalResult=0;Counters originalCounts;int originalError=0;
  for(bool cooperative:{false,true}){
   reset();FsFile f;bind(f);std::vector<uint8_t> data(5*4096,0x55);const void* ptr=data.data();size_t size=data.size();
   if(kind==0) { lockFails=true; }if(kind==1) { ready=false; }if(kind==2) { f.impl->writer=false; }if(kind==3) { f.impl->handle=0; }if(kind==4) { ptr=nullptr; }if(kind==5) { size=kMaxOperationBytes+1; }
   if(kind==6) { shortCall=2; }if(kind==7) { zeroCall=1; }if(kind==8) { errorCall=2; }if(kind==9){f.impl->syncWrites=true;syncFault=2;}if(kind==10) { providerMillis=5000; }if(kind==11) { size=0; }
   HalWriteBudget b([](){return millis();},[](){vTaskDelay(1);});const size_t result=writePath(f,ptr,size,cooperative,b);
   if(!cooperative){originalTrace=trace;originalData=f.impl->data;originalResult=result;originalCounts=counts;originalError=f.impl->error;}
   else{assert(trace==originalTrace&&f.impl->data==originalData&&result==originalResult&&f.impl->error==originalError);assert(counts.writes==originalCounts.writes&&counts.bytes==originalCounts.bytes&&counts.locks==originalCounts.locks&&counts.mutations==originalCounts.mutations&&counts.syncs==originalCounts.syncs&&counts.errors==originalCounts.errors);}
   if(kind==10) { assert(result==4*4096&&counts.writes==4&&f.impl->error); } // original 20s cap, no inflated deadline
  }
 }
 reset();{FsFile f;f.impl.reset();HalWriteBudget b([](){return millis();},[](){vTaskDelay(1);});assert(f.writeCooperatively(nullptr,0,b)==0&&counts.mutations==0&&counts.writes==0);}
 reset();HalWriteBudget count([](){return millis();},[](){vTaskDelay(1);});for(unsigned i=0;i<31;++i)count.afterWrite(1);assert(!counts.yields);count.afterWrite(1);assert(counts.yields==1);
 reset();HalWriteBudget bytes([](){return millis();},[](){vTaskDelay(1);});bytes.afterWrite(4095);assert(!counts.yields);bytes.afterWrite(1);assert(counts.yields==1);
 reset();HalWriteBudget time([](){return millis();},[](){vTaskDelay(1);});clockMs=7;time.checkpoint();assert(!counts.yields);clockMs=8;time.checkpoint();assert(counts.yields==1);
 reset();clockMs=UINT32_MAX-3ull;HalWriteBudget wrap([](){return millis();},[](){vTaskDelay(1);});clockMs+=8;wrap.checkpoint();assert(counts.yields==1);
 // Time checkpoint before lock/guard failure and after unsuccessful provider progress.
 for(unsigned kind=0;kind<3;++kind){reset();FsFile f;bind(f);HalWriteBudget b([](){return millis();},[](){vTaskDelay(1);});uint8_t c=0;clockMs=8;if(kind==0) { lockFails=true; }else{providerMillis=8;if(kind==1) { zeroCall=1; }else errorCall=1;}f.writeCooperatively(&c,1,b);assert(counts.yields==(kind==0?1u:2u));}
 reset();FsFile f;bind(f);HalWriteBudget b([](){return millis();},[](){vTaskDelay(1);});for(unsigned i=0;i<100;++i){clockMs+=2;f.writeCooperatively(nullptr,0,b);}assert(counts.yields==25&&maxYieldGap<=8&&counts.writes==0&&counts.mutations==100);
}
#endif
int main(){
 for(unsigned lines:{12u,24u,36u})for(bool focus:{false,true})for(unsigned pages:{1u,10u,32u})healthy(lines,10,focus,pages);
 for(unsigned words:{0u,1u,7u,32u,200u})for(bool focus:{false,true})healthy(4,words,focus,1,true,true);
 healthy(24,10,false,1,false,false,3);healthy(24,10,true,1,false,false,3,true);healthy(24,10,true,1,false,false,0,true);healthy(1,10000,false);healthy(1,10000,true);largeString();failures();
#ifndef TEST_ORIGINAL_HAL
 halBounds();
#endif
 std::cerr<<"PASS: write parity/cost, faults/retry, guards and cooperative bounds\n";
}
