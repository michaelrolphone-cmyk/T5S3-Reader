#include "M5WispRefresh.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <vector>
using namespace NativeVideoBootScrub;
struct Hooks {
  unsigned time=0, allocations=0, stopAt=~0U;
  bool fail=false;
  uint8_t* alloc(size_t bytes){if(fail)return nullptr;++allocations;return (uint8_t*)malloc(bytes);}
  void free(uint8_t* p){assert(allocations);--allocations;::free(p);}
  unsigned now(){return time;}
  void pause(){++time;}
  bool stopped(){return time>=stopAt;}
};
struct Bus {
  std::vector<uint8_t> black,white,draw;
  unsigned row=0,scan=0;
  bool powered=false;
  uint8_t* liveSource=nullptr;
  Bus():black(kMapBytes),white(kMapBytes),draw(kMapBytes){}
  bool powerControl(bool on){powered=on;return true;}
  void beginTransaction(){assert(powered);row=0;}
  void scanlineDone(){}
  void endTransaction(){assert(row==kHeight);++scan;}
  void writeScanLine(const uint8_t* data,unsigned length){
    assert(length==kWidth/4+8);
    for(unsigned p=kWidth/4;p<length;++p)assert(!data[p]);
    for(unsigned x=0;x<kWidth;++x){
      const unsigned command=(data[x/4]>>(6-2*(x%4)))&3;assert(command!=3);
      unsigned i=row*kWidth+x;
      if(scan<kScans){black[i]+=command==1;white[i]+=command==2;}
      else {assert(command!=2);draw[i]+=command==1;}
    }
    if(liveSource && scan==0 && row==0)memset(liveSource,255,kMapBytes/2);
    ++row;
  }
};
int main(){
 std::vector<uint8_t> source(kMapBytes/2);
 std::vector<uint16_t> state(kMapBytes,0x8888);
 uint8_t dma0[kWidth/4+8],dma1[kWidth/4+8];
 // Region geometry, including single-row/column-pair updates and odd heights.
 const unsigned regions[][4]={{0,0,kWidth,kHeight},{2,127,82,23},
   {14,20,126,127},{958,0,2,540},{0,539,960,1},{40,100,2,1}};
 for(const auto& region:regions){
   for(unsigned i=0;i<source.size();++i)source[i]=(i&1)?0xaf:0x05;
   auto original=source;std::fill(state.begin(),state.end(),0x8888);
   Hooks hooks;Bus bus;bus.liveSource=source.data(); // concurrent next request
   unsigned x=region[0],y=region[1],w=region[2],h=region[3];
   assert(M5WispRefresh::run(&bus,hooks,source.data(),state.data(),dma0,dma1,sizeof(dma0),x,y,w,h,42));
   assert(!hooks.allocations && bus.scan==kScans+3);
   assert(hooks.time>=(kScans+3)*42 && hooks.time<1100);
   for(unsigned py=0;py<kHeight;++py)for(unsigned px=0;px<kWidth;++px){
     unsigned i=py*kWidth+px;bool inside=px>=x&&px<x+w&&py>=y&&py<y+h;
     assert(bus.black[i]==(inside?3:0));assert(bus.white[i]==(inside?6:0));
     assert(bus.draw[i]==(inside?M5WispRefresh::shadePulses(original[i/2],px):0));
     assert(state[i]==(inside?(0x8000|(42<<8)|original[i/2]):0x8888));
   }
 }
 // Translation invariance proves the pattern belongs to the requested region,
 // not a clipped full-panel field. Sentinel checks include all four boundaries.
 for(const auto& shape: {std::pair<unsigned,unsigned>{82,23},{126,127},{2,540},{960,1}}){
   const unsigned w=shape.first,h=shape.second;
   const unsigned ox=w<900?8:0,oy=h<500?9:0;
   std::vector<uint8_t> local(kMapBytes,255),moved(kMapBytes,255);
   const unsigned grid=h>=64?kGrid:1;
   for(unsigned row=0;row<h;row+=grid){
     M5WispRefresh::buildRegionBand(local.data(),0,0,w,h,row);
     M5WispRefresh::buildRegionBand(moved.data(),ox,oy,w,h,row);
   }
   unsigned arrivals[8]={};
   for(unsigned py=0;py<kHeight;++py)for(unsigned px=0;px<kWidth;++px){
     const auto value=moved[py*kWidth+px];
     if(px>=ox&&px<ox+w&&py>=oy&&py<oy+h){
       assert(value<=kLastArrival);++arrivals[value];
       assert(value==local[(py-oy)*kWidth+px-ox]);
     }else assert(value==255);
   }
   unsigned distinct=0;for(auto count:arrivals)distinct+=count!=0;
   assert(distinct>=6); // even thin strips retain spatial motion
 }
 Hooks fail;fail.fail=true;Bus a;
 assert(!M5WispRefresh::run(&a,fail,source.data(),state.data(),dma0,dma1,sizeof(dma0),0,0,kWidth,kHeight,42));
 assert(!a.powered&&!fail.allocations);
 Hooks cancel;cancel.stopAt=150;Bus b;std::fill(state.begin(),state.end(),0x8888);
 assert(!M5WispRefresh::run(&b,cancel,source.data(),state.data(),dma0,dma1,sizeof(dma0),0,0,kWidth,kHeight,42));
 assert(!cancel.allocations);for(auto v:state)assert(v==0x8888);
 puts("static wisp: full/partial coverage, unchanged cleaning dose, 4 gray levels, immutable targets, cancellation and allocation fallback PASS");
}
