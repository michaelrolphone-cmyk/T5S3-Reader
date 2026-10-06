#include <HalStorage.h>
#include <GfxRenderer.h>
#include <freertos/task.h>
#include <cstdlib>
#include <cstdio>
#include <array>
#include "ImageBlock.h"
#include "ImageDecoderFactory.h"
#include "DirectPixelWriter.h"

TestStorage Storage;
static size_t liveAllocations=0, largestAllocation=0;
static bool failAllocation=false, decoderSucceeds=false;
static unsigned decoderCalls=0;
void* imageTestAllocate(size_t n) {
  largestAllocation=std::max(largestAllocation,n);
  if(failAllocation)return nullptr;
  void* p=std::malloc(n);if(p)++liveAllocations;return p;
}
void imageTestFree(void* p){if(p){assert(liveAllocations);--liveAllocations;}std::free(p);}
class Decoder final:public ImageToFramebufferDecoder {
 public:
 bool decodeToFramebuffer(const std::string&,GfxRenderer& renderer,const RenderConfig& config) override {
   ++decoderCalls;if(!decoderSucceeds)return false;
   DirectPixelWriter writer;writer.init(renderer);
   for(int y=0;y<config.maxHeight;++y){writer.beginRow(config.y+y);for(int x=0;x<config.maxWidth;++x)writer.writePixel(config.x+x,1);}
   return true;
 }
 bool getDimensions(const std::string&,ImageDimensions&) const override{return false;}
 const char* getFormatName()const override{return "fixture";}
};
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&){static Decoder decoder;return &decoder;}

static uint64_t hashBytes(const uint8_t* data,size_t count){uint64_t h=1469598103934665603ull;while(count--){h^=*data++;h*=1099511628211ull;}return h;}
static uint64_t traceHash(){return hashBytes(reinterpret_cast<const uint8_t*>(trace.data()),trace.size()*sizeof(uint64_t));}
static void reset(){
 assert(!Storage.openFiles&&!liveAllocations);Storage.files.clear();trace.clear();
 readCalls=readBytes=readLocks=requestedDelays=rowYields=clockMs=lastYieldClock=maxYieldGap=0;
 clockStep=providerMillis=0;slowOffset=0;errorOffset=maxProviderRead=mediaLossOffset=SIZE_MAX;providerError=false;ready=true;failLock=false;
 failAllocation=decoderSucceeds=false;decoderCalls=0;largestAllocation=0;
}
static std::vector<uint8_t> cacheFor(uint16_t w,uint16_t h){
 std::vector<uint8_t> cache(4+((w+3)/4)*h,0);std::memcpy(cache.data(),&w,2);std::memcpy(cache.data()+2,&h,2);
 for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)cache[4+y*((w+3)/4)+x/4]|=((x/7+y/9)%4)<<(6-2*(x%4));
 return cache;
}
static void expectedPixel(std::vector<uint8_t>& frame,const GfxRenderer& r,int x,int y,unsigned shade){
 int px=0,py=0;
 switch(r.orientation){
 case GfxRenderer::Portrait:px=y;py=539-x;break;
 case GfxRenderer::PortraitInverted:px=959-y;py=x;break;
 case GfxRenderer::LandscapeClockwise:px=959-x;py=539-y;break;
 case GfxRenderer::LandscapeCounterClockwise:px=x;py=y;break;
 }
 bool draw=r.mode==GfxRenderer::BW?shade<3:r.mode==GfxRenderer::GRAYSCALE_MSB?(shade==1||shade==2):shade==1;
 if(draw){if(r.mode==GfxRenderer::BW)frame[py*120+px/8]&=~(1<<(7-px%8));else frame[py*120+px/8]|=1<<(7-px%8);}
}
static void run(const char* label,uint16_t width,uint16_t height,GfxRenderer::Orientation orientation,
                GfxRenderer::RenderMode mode,int fault=0,unsigned passes=1){
 reset();auto cache=cacheFor(width,height);
 // Faults are repeatable on each pass, and failure never changes request boundaries.
 if(fault==1)cache.resize(4+((width+3)/4)*17+1); // truncated after17 complete rows
 if(fault==2||fault==3||fault==11)errorOffset=4+((width+3)/4)*17;
 if(fault==3)decoderSucceeds=true;
 if(fault==4)cache.resize(3);
 if(fault==5)cache[0]^=0x40;
 if(fault==6)failAllocation=true;
 if(fault==7)maxProviderRead=7; // HAL completes short-positive reads, same sequence
 if(fault==8)ready=false;
 if(fault==9)failLock=true;
 if(fault==12)mediaLossOffset=4+((width+3)/4)*17;
 if(fault==13)providerMillis=3;
 if(fault==14)providerMillis=9;
 if(fault==15||fault==16){providerMillis=5000;maxProviderRead=7;slowOffset=fault==16?4+((width+3)/4)*17:0;}
 if(fault!=10)Storage.files["/image.jpg.pxc2"]=cache;
 if(fault!=11)Storage.files["/image.jpg"]={1};
 GfxRenderer renderer;renderer.orientation=orientation;renderer.mode=mode;
 for(unsigned pass=0;pass<passes;++pass){
   providerError=false;renderer.clear();ImageBlock image("/image.jpg",width,height);image.render(renderer,10,20);
   std::vector<uint8_t> expected(sizeof(renderer.framebuffer),mode==GfxRenderer::BW?0xff:0);
   unsigned rows=height;
   if(fault==1||fault==2||fault==3||fault==11||fault==12||fault==16)rows=17;
   if(fault==4||fault==5||fault==6||fault==8||fault==9||fault==10||fault==15)rows=0;
   for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<width;++x)expectedPixel(expected,renderer,10+x,20+y,(x/7+y/9)%4);
   if(fault==3)for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)expectedPixel(expected,renderer,10+x,20+y,1);
   assert(std::memcmp(expected.data(),renderer.framebuffer,expected.size())==0);
   assert(!Storage.openFiles&&!liveAllocations);
 }
 assert(largestAllocation<=((width+3u)/4));
 if(fault==0){assert(readCalls==passes*(height*(width>0)+2u));assert(readBytes==passes*cache.size());
#ifdef TEST_BASELINE
   assert(requestedDelays==readCalls);
#else
   const unsigned interval=width>0?std::min<unsigned>(31, (4096+((width+3)/4)-1)/((width+3)/4)):32;
   assert(requestedDelays+rowYields<=passes*(2u+(height+interval-1)/interval));
#endif
 }
 if(fault==13||fault==14)assert(maxYieldGap<=8+providerMillis);
 if(fault==15||fault==16){assert(providerMillis==5000);assert(decoderCalls==passes);assert(!providerError); /* HAL20s deadline, not a provider error. */
   assert(readCalls==passes*(fault==15?6u:312u));assert(readBytes==passes*(fault==15?32u:2072u));}
 printf("%s fault=%d %ux%u o=%d m=%d passes=%u frame=%016llx trace=%016llx reads=%llu bytes=%llu decode=%u alloc=%zu cleanup=PASS\n",label,fault,width,height,int(orientation),int(mode),passes,(unsigned long long)hashBytes(renderer.framebuffer,sizeof(renderer.framebuffer)),(unsigned long long)traceHash(),(unsigned long long)readCalls,(unsigned long long)readBytes,decoderCalls,largestAllocation);
 fprintf(stderr,"cost %s %ux%u passes=%u waits=%llu row_yields=%llu\n",label,width,height,passes,(unsigned long long)requestedDelays,(unsigned long long)rowYields);
}
static void budgetBounds(){
#ifndef TEST_BASELINE
 reset();HalReadBudget b(millis, []() { vTaskDelay(1); });
 for(int i=0;i<31;++i){b.afterRead(1);}assert(rowYields==0);b.afterRead(1);assert(rowYields==1);
 reset();HalReadBudget bytes(millis, []() { vTaskDelay(1); });bytes.afterRead(4095);assert(!rowYields);bytes.afterRead(1);assert(rowYields==1);
 reset();HalReadBudget rows(millis, []() { vTaskDelay(1); });for(int i=0;i<32;++i)rows.afterRow();assert(rowYields==1);
 reset();HalReadBudget slow(millis, []() { vTaskDelay(1); });clockMs=7;slow.checkpoint();assert(!rowYields);clockMs=8;slow.checkpoint();assert(rowYields==1);
 reset();clockMs=UINT32_MAX-3ull;HalReadBudget wrap(millis, []() { vTaskDelay(1); });clockMs+=8;wrap.checkpoint();assert(rowYields==1);
 reset();HalReadBudget perOperation(millis, []() { vTaskDelay(1); });perOperation.afterRead(4096);assert(rowYields==1);HalReadBudget next(millis, []() { vTaskDelay(1); });next.checkpoint();assert(rowYields==1);
 // CPU and slow-provider clocks are sampled each row/read; no busy work interval
 // exceeds8ms plus one bounded external operation. No device latency is inferred.
 reset();HalReadBudget cpu(millis, []() { vTaskDelay(1); });for(int i=0;i<80;++i){clockMs+=2;cpu.afterRow();}assert(maxYieldGap<=8&&rowYields==20);
#endif
}
int main(){
 for(int o=0;o<4;++o)for(int m=0;m<3;++m){auto orientation=static_cast<GfxRenderer::Orientation>(o);auto mode=static_cast<GfxRenderer::RenderMode>(m);
  run("healthy",o%2?800:480,o%2?480:800,orientation,mode,0,2);
  run("odd-width",31,65,orientation,mode);
  for(int fault=1;fault<=16;++fault)run("fault",480,100,orientation,mode,fault);
 }
 run("tiny",1,1,GfxRenderer::Portrait,GfxRenderer::BW);run("zero-width",0,33,GfxRenderer::Portrait,GfxRenderer::BW);run("zero-height",12,0,GfxRenderer::Portrait,GfxRenderer::BW);run("retry",480,800,GfxRenderer::Portrait,GfxRenderer::BW,0,2);
 budgetBounds();
}
