#include <cassert>
#include <iostream>
#include <iomanip>
#include <GfxRenderer.h>
#include <Serialization.h>
#include <freertos/task.h>
#include "Page.h"
static size_t providerRead(void* ctx,unsigned,void* target,size_t want){
 auto& f=*static_cast<FileImpl*>(ctx);++counts.reads;clockMs+=providerMillis;
 const size_t available=f.pos<f.data.size()?f.data.size()-f.pos:0;
 const size_t got=counts.reads==zeroCall?0:std::min({want,available,maxRead});
 trace.insert(trace.end(),{f.pos,want,got});
 if(got)std::memcpy(target,f.data.data()+f.pos,got);
 f.pos+=got;counts.bytes+=got;providerError=counts.reads==errorCall;return got;
}
static bool handleError(void*,unsigned,bool){return providerError;}
Volume v{nullptr,providerRead};Extended e{handleError};Volume* volume=&v;Extended* extended=&e;
ImageBlock::ImageBlock(const std::string& p,int16_t w,int16_t h):imagePath(p),width(w),height(h){}
void ImageBlock::render(GfxRenderer& renderer,int x,int y){renderer.commands.push_back("image:"+imagePath+":"+std::to_string(x)+":"+std::to_string(y)+":"+std::to_string(width)+":"+std::to_string(height));}
#include "ImageSerialization.inc"
class Section {public:static constexpr unsigned HEADER_SIZE=32;FsFile file;std::string filePath="fixture";int currentPage=0;std::unique_ptr<Page> loadPageFromSectionFile();};
#include "SectionRead.inc"
static uint64_t hash(const void* ptr,size_t n){const auto* p=static_cast<const uint8_t*>(ptr);uint64_t h=1469598103934665603ull;while(n--)h=(h^*p++)*1099511628211ull;return h;}
static void reset(){counts={};trace.clear();clockMs=lastYield=maxYieldGap=0;providerMillis=0;maxRead=errorCall=zeroCall=SIZE_MAX;providerError=false;ready=true;lockFails=openFails=closeFails=false;}
static std::vector<uint8_t> payload(Page& page){FsFile f;assert(page.serialize(f));return f.impl->data;}
static std::vector<uint8_t> encode(const std::vector<uint8_t>& page){FsFile f;std::vector<uint8_t> header(32,0);f.write(header.data(),header.size());f.write(page.data(),page.size());uint32_t pagePos=32,lut=f.impl->pos;serialization::writePod(f,pagePos);f.seek(20);serialization::writePod(f,lut);return f.impl->data;}
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
static void print(const std::string& name,const std::vector<uint8_t>& bytes,const GfxRenderer& r){
 std::string commands;for(const auto& x:r.commands)commands+=x+'\n';
 std::cout<<name<<" reads="<<counts.reads<<" bytes="<<counts.bytes<<" locks="<<counts.locks<<" trace="<<std::hex<<hash(trace.data(),trace.size()*sizeof(uint64_t))<<" payload="<<hash(bytes.data(),bytes.size())<<" render_commands="<<hash(commands.data(),commands.size())<<std::dec<<" opens="<<counts.opens<<" closes="<<counts.closes<<" seeks="<<counts.seeks<<"\n";
 std::cerr<<"cost "<<name<<" waits="<<counts.delays+counts.yields<<" ordinary="<<counts.delays<<" budget="<<counts.yields<<"\n";
}
static void healthy(unsigned lines,unsigned words,bool focus,bool varied=false,bool mixed=false,size_t cap=SIZE_MAX,unsigned slow=0){
 auto original=pageFor(lines,words,focus,varied,mixed);auto bytes=payload(original);Storage.data=encode(bytes);
 reset();maxRead=cap;providerMillis=slow;
 Section section;auto loaded=section.loadPageFromSectionFile();assert(loaded);assert(!section.file.impl->opened);
 GfxRenderer expected,actual;original.render(expected,42,13,-9);loaded->render(actual,42,13,-9);assert(expected.commands==actual.commands);
 assert(payload(*loaded)==bytes);assert(counts.opens==1&&counts.closes==1&&counts.seeks==3);
 if(!varied&&!mixed&&cap==SIZE_MAX){assert(counts.reads==4+lines*(17+(focus?6:4)*words));assert(counts.bytes==bytes.size()+8);assert(counts.locks==counts.reads);
#ifdef TEST_BASELINE
 assert(counts.delays==counts.reads&&!counts.yields);
#else
 if(!slow){assert(counts.delays==4+3*lines);assert(counts.yields==lines*((14+(focus?6:4)*words)/32));}
#endif
 }
 if(slow)assert(maxYieldGap<=8+slow);
 print("healthy:"+std::to_string(lines)+":"+std::to_string(words)+":"+std::to_string(focus)+":"+std::to_string(varied)+":"+std::to_string(mixed)+":"+std::to_string(cap)+":"+std::to_string(slow),bytes,actual);
 // A fresh operation after every healthy/mixed/short/slow pass must reopen and produce the same output.
 reset();auto retry=section.loadPageFromSectionFile();assert(retry&&payload(*retry)==bytes);assert(counts.opens==1&&counts.closes==1&&!section.file.impl->opened);
}
static void failures(){
 auto original=pageFor(3,10,true,true,true);auto bytes=payload(original);const auto data=encode(bytes);
 reset();Storage.data=data;Section countSection;auto valid=countSection.loadPageFromSectionFile();assert(valid);const auto total=counts.reads;
 // Error-after-copy retains every initialized scalar. Historical unchecked/truncated
 // scalar handling (BUG39/196) is unchanged and is not invoked with indeterminate data.
 for(size_t fault:{1u,2u,3u,4u,7u,15u,32u,64u,99u,unsigned(total-3),unsigned(total)}){
  reset();Storage.data=data;errorCall=fault;Section section;auto loaded=section.loadPageFromSectionFile();
  assert(counts.opens==1&&counts.closes==1&&!section.file.impl->opened);
  GfxRenderer output;std::vector<uint8_t> out;if(loaded){out=payload(*loaded);assert(out==bytes);loaded->render(output,42,13,-9);}
  print("error-after-copy:"+std::to_string(fault)+":"+std::to_string(bool(loaded)),out,output);
  reset();auto retry=section.loadPageFromSectionFile();assert(retry&&payload(*retry)==bytes&&!section.file.impl->opened);
 }
 for(unsigned fault=0;fault<5;++fault){
  reset();Storage.data=data;
  if(fault==0)openFails=true;
  if(fault==1)Storage.data[34]=99; // initialized unknown element tag
  if(fault==2){const size_t fnOffset=32+bytes.size()-2*sizeof(FootnoteEntry)-sizeof(uint16_t);uint16_t n=17;memcpy(Storage.data.data()+fnOffset,&n,2);}
  if(fault==3){const size_t fnOffset=32+bytes.size()-2*sizeof(FootnoteEntry)-sizeof(uint16_t);uint16_t n=3;memcpy(Storage.data.data()+fnOffset,&n,2);} // checked extra footnote reaches short read/EOF
  if(fault==4)zeroCall=total; // zero progress on checked final footnote
  Section section;auto loaded=section.loadPageFromSectionFile();assert(!loaded);assert(!section.file.impl->opened);
  GfxRenderer output;print("failure:"+std::to_string(fault),{},output);
  reset();Storage.data=data;auto retry=section.loadPageFromSectionFile();assert(retry&&payload(*retry)==bytes&&!section.file.impl->opened);
 }
 reset();Storage.data=data;{Section section;closeFails=true;auto loaded=section.loadPageFromSectionFile();assert(loaded&&section.file.impl->opened);closeFails=false;assert(section.file.close());assert(!section.file.impl->opened&&counts.closes==2);} // same unchecked close/retry behavior
 reset();FsFile over;uint16_t tooMany=10001;serialization::writePod(over,tooMany);over.seek(0);volume->context=over.impl.get();auto block=TextBlock::deserialize(over);assert(!block);assert(counts.reads==1);
}
static void largeString(){
 reset();
 std::vector<std::string> words{std::string(9000,'x')};std::vector<int16_t> xs{-7};std::vector<EpdFontFamily::Style> styles{EpdFontFamily::ITALIC};
 Page original;original.elements.push_back(std::make_shared<PageLine>(std::make_shared<TextBlock>(words,xs,styles),3,4));
 auto bytes=payload(original);Storage.data=encode(bytes);reset();Section section;auto loaded=section.loadPageFromSectionFile();assert(loaded&&payload(*loaded)==bytes&&!section.file.impl->opened);
 GfxRenderer expected,actual;original.render(expected,42,0,0);loaded->render(actual,42,0,0);assert(expected.commands==actual.commands);print("large-string",bytes,actual);
#ifndef TEST_BASELINE
 assert(counts.yields==2); // two 4096-byte checkpoints inside the unchanged large read
#endif
}
static void halBounds(){
 for(unsigned fault=0;fault<7;++fault){
  reset();HalFile f;f.impl->data.assign(10,0x55);volume->context=f.impl.get();uint8_t out[10]{};HalReadBudget budget([]() -> uint32_t { return millis(); },[](){vTaskDelay(1);});
  if(fault==0)lockFails=true;
  if(fault==1)ready=false;
  if(fault==2)f.impl->directory=true;
  if(fault==3)f.impl->handle=0;
  if(fault==4)errorCall=1;
  if(fault==5)zeroCall=1;
  if(fault==6){providerMillis=5000;maxRead=1;}
  const int result=f.readCooperatively(out,sizeof(out),budget);
  assert(result==(fault==5?0:fault==6?4:-1));
  if(fault==6){assert(counts.reads==4&&counts.bytes==4&&f.impl->error);}
 }
 reset();HalFile f;f.impl->data.assign(10,1);volume->context=f.impl.get();HalReadBudget b([]() -> uint32_t { return millis(); },[](){vTaskDelay(1);});assert(f.readCooperatively(nullptr,1,b)==-1);assert(f.readCooperatively(nullptr,kMaxOperationBytes+1,b)==-1);uint8_t out=0;assert(f.readCooperatively(&out,kMaxOperationBytes+1,b)==-1&&f.impl->error);
 reset();HalReadBudget count([]() -> uint32_t { return millis(); },[](){vTaskDelay(1);});for(unsigned i=0;i<31;++i)count.afterRead(1);assert(!counts.yields);count.afterRead(1);assert(counts.yields==1);
 reset();HalReadBudget bytes([]() -> uint32_t { return millis(); },[](){vTaskDelay(1);});bytes.afterRead(4095);assert(!counts.yields);bytes.afterRead(1);assert(counts.yields==1);
 reset();HalReadBudget time([]() -> uint32_t { return millis(); },[](){vTaskDelay(1);});clockMs=7;time.checkpoint();assert(!counts.yields);clockMs=8;time.checkpoint();assert(counts.yields==1);
 reset();clockMs=UINT32_MAX-3ull;HalReadBudget wrap([]() -> uint32_t { return millis(); },[](){vTaskDelay(1);});clockMs+=8;wrap.checkpoint();assert(counts.yields==1);
 reset();HalReadBudget cpu([]() -> uint32_t { return millis(); },[](){vTaskDelay(1);});for(unsigned i=0;i<80;++i){clockMs+=2;cpu.checkpoint();}assert(counts.yields==20&&maxYieldGap<=8);
}
int main(){
 for(unsigned l:{12u,24u,36u})for(bool f:{false,true})healthy(l,10,f);
 for(unsigned w:{0u,1u,7u,32u,200u})for(bool f:{false,true})healthy(4,w,f,true,true);
 healthy(3,10,true,true,true,1);healthy(3,10,true,true,true,7);
 healthy(2,10,true,false,true,SIZE_MAX,3);healthy(2,10,true,false,true,SIZE_MAX,9);
 healthy(1,10000,false);largeString();failures();halBounds();
 std::cout<<"error/retry/cleanup/cooperation controls PASS\n";
}
