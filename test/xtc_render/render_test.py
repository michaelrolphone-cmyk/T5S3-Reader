#!/usr/bin/env python3
"""Compile complete XTC renderPage and real pixel/dither/rotation code."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get("XTC_RENDER_SOURCE_ROOT", ROOT))
def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start)+1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
gfx = (ROOT/"lib/GfxRenderer/GfxRenderer.cpp").read_text()
display = (ROOT/"lib/hal/HalDisplay.cpp").read_text()
reader = (SOURCE/"src/activities/reader/XtcReaderActivity.cpp").read_text()
prefix = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
template<class... T> void logMessage(T...){}
#define LOG_DBG(...) logMessage(__VA_ARGS__)
#define LOG_ERR(...) logMessage(__VA_ARGS__)
constexpr int UI_12_FONT_ID=12,STR_MEMORY_ERROR=1,STR_PAGE_LOAD_ERROR=2;
const char* tr(int){return "";}
struct EpdFontFamily{enum {BOLD};};
enum class DisplayPresentMode{Clean,Quality};
enum Color : uint8_t {Clear=0,White=1,LightGray=5,DarkGray=10,Black=16};
constexpr uint8_t kGrayWhite=255,kGrayBlack=0,kGrayDark=85,kGrayLight=170;
unsigned allocations=0,frees=0,live=0,yields=0,watchdogs=0;
uint32_t clockMs=0,clockStep=0;
bool allocationFail=false,loadFail=false;
uint32_t millis(){const auto result=clockMs;clockMs+=clockStep;return result;}
void esp_task_wdt_reset(){++watchdogs;}
void vTaskDelay(unsigned ticks){assert(ticks==1);++yields;}
void* pageMalloc(size_t n){++allocations;if(allocationFail)return nullptr;++live;return std::malloc(n);}
void pageFree(void*p){assert(p&&live);--live;++frees;std::free(p);}
struct Xtc {
 uint16_t w=32,h=24;uint8_t depth=2;unsigned loads=0;
 int pattern=-1;
 uint8_t tone(unsigned x,unsigned y) const {
  return pattern<0 ? static_cast<uint8_t>((x/4+3*(y/4))%4) : pattern;
 }
 uint16_t getPageWidth(){return w;}uint16_t getPageHeight(){return h;}
 uint8_t getBitDepth(){return depth;}unsigned getPageCount(){return 3;}
 int getLastError(){return 0;}
 size_t loadPage(unsigned page,uint8_t*p,size_t n){
  assert(page==0);++loads;if(loadFail)return 0;
  if(depth==2){
   const size_t plane=(size_t(w)*h+7)/8;
   assert(h%8==0&&n==plane*2);std::memset(p,0,n);
   for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
    const size_t at=(w-1-x)*(h/8)+y/8;const uint8_t mask=1u<<(7-y%8);
    const uint8_t value=tone(x,y);
    if(value&2)p[at]|=mask;if(value&1)p[plane+at]|=mask;
   }
  }else{
   assert(n==size_t((w+7)/8)*h);std::memset(p,255,n);
   for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
    if(tone(x,y)>=1)p[y*((w+7)/8)+x/8]&=~(1u<<(7-x%8));
  }
  return n;
 }
};
namespace xtc{const char*errorToString(int){return "read failed";}}
struct GfxRenderer {
 enum Orientation{Portrait,LandscapeClockwise,PortraitInverted,LandscapeCounterClockwise};
 bool initialized=true,grayAvailable=false;
 uint16_t panelWidth,panelHeight,panelWidthBytes,visibleWidth,visibleHeight;
 Orientation orientation=Portrait;
 const void*glyphSource_=nullptr;
 std::vector<uint8_t> guarded,base,lsb,msb,shown,grayShown;
 uint8_t*frameBuffer=nullptr;
 unsigned monoPresents=0,grayPresents=0,captures=0,cleanups=0,errors=0;
 DisplayPresentMode lastMode=DisplayPresentMode::Quality;
 GfxRenderer(unsigned w,unsigned h):panelWidth(w),panelHeight(h),panelWidthBytes(w/8),
   visibleWidth(h),visibleHeight(w),guarded(size_t(w)*h/8+32,0xa5){
  frameBuffer=guarded.data()+16;clearScreen();
 }
 int getScreenWidth()const;int getScreenHeight()const;
 void drawPixel(int,int,bool)const;
 template<Color color>void drawPixelDither(int,int)const;
 void fillRectDither(int,int,int,int,Color)const;
 void fillRect(int x,int y,int w,int h,bool black)const{
  for(int py=y;py<y+h;++py)for(int px=x;px<x+w;++px)drawPixel(px,py,black);
 }
 void clearScreen(uint8_t value=255){std::memset(frameBuffer,value,size_t(panelWidthBytes)*panelHeight);}
 void drawCenteredText(int,int,const char*,bool=false,int=0){++errors;}
 size_t bytes()const{return size_t(panelWidthBytes)*panelHeight;}
 void displayBuffer(DisplayPresentMode mode=DisplayPresentMode::Quality){
  ++monoPresents;lastMode=mode;shown.assign(frameBuffer,frameBuffer+bytes());
 }
 bool captureGrayscaleBaseBuffer(){++captures;if(!grayAvailable)return false;base.assign(frameBuffer,frameBuffer+bytes());return true;}
 void copyGrayscaleLsbBuffers(){lsb.assign(frameBuffer,frameBuffer+bytes());}
 void copyGrayscaleMsbBuffers(){msb.assign(frameBuffer,frameBuffer+bytes());}
 void displayGrayBuffer(DisplayPresentMode mode);
 void cleanupGrayscaleWithFrameBuffer(){++cleanups;base.clear();lsb.clear();msb.clear();}
 void checkGuards(){
  for(unsigned i=0;i<16;++i)assert(guarded[i]==0xa5&&guarded[guarded.size()-1-i]==0xa5);
 }
};
'''
production = '\n'.join(method(gfx,s) for s in (
    'static inline void rotateCoordinates(',
    'void GfxRenderer::drawPixel(',
    'int GfxRenderer::getScreenWidth()',
    'int GfxRenderer::getScreenHeight()'))
production += '\ntemplate <>\n' + method(gfx,'void GfxRenderer::drawPixelDither<Color::LightGray>')
production += '\ntemplate <>\n' + method(gfx,'void GfxRenderer::drawPixelDither<Color::DarkGray>')
production += '\n' + method(gfx,'void GfxRenderer::fillRectDither(')
production += '\n' + method(display,'uint8_t grayscaleValueForBit(')
scaffold = r'''
void GfxRenderer::displayGrayBuffer(DisplayPresentMode mode){
 assert(grayAvailable&&base.size()==bytes()&&lsb.size()==bytes()&&msb.size()==bytes());
 ++grayPresents;lastMode=mode;grayShown.resize(size_t(panelWidth)*panelHeight);
 for(unsigned y=0;y<panelHeight;++y)for(unsigned x=0;x<panelWidth;++x){
  const size_t offset=y*panelWidthBytes+x/8;const uint8_t bit=1u<<(7-x%8);
  grayShown[y*panelWidth+x]=grayscaleValueForBit(base[offset],lsb[offset],msb[offset],bit);
 }
}
namespace ReaderUtils {
 DisplayPresentMode takeReaderRefreshMode(int& remaining){
  if(remaining<=1){remaining=5;return DisplayPresentMode::Clean;}
  --remaining;return DisplayPresentMode::Quality;
 }
 void displayWithRefreshCycle(GfxRenderer&r,int& remaining){r.displayBuffer(takeReaderRefreshMode(remaining));}
}
struct XtcReaderActivity{
 std::unique_ptr<Xtc>xtc=std::make_unique<Xtc>();
 GfxRenderer renderer;unsigned currentPage=0;int pagesUntilFullRefresh=1;
 XtcReaderActivity(unsigned w,unsigned h):renderer(w,h){}
 void renderPage();
};
#define malloc pageMalloc
#define free pageFree
'''
tests = r'''
#undef malloc
#undef free
void logical(const GfxRenderer&r,unsigned px,unsigned py,int&x,int&y){
 // Independent inverse transform oracle, not the production mapping.
 switch(r.orientation){
  case GfxRenderer::Portrait:x=r.panelHeight-1-py;y=px;break;
  case GfxRenderer::LandscapeClockwise:x=r.panelWidth-1-px;y=r.panelHeight-1-py;break;
  case GfxRenderer::PortraitInverted:x=py;y=r.panelWidth-1-px;break;
  default:x=px;y=py;break;
 }
}
bool blackFor(uint8_t tone,int x,int y){
 return tone==3 || (tone==1 && ((x+y)&1)==0) || (tone==2 && !(x&1) && !(y&1));
}
void verify(XtcReaderActivity&a,bool gray){
 auto&r=a.renderer;
 for(unsigned py=0;py<r.panelHeight;++py)for(unsigned px=0;px<r.panelWidth;++px){
  int x,y;logical(r,px,py,x,y);
  const uint8_t tone=x<a.xtc->w&&y<a.xtc->h?a.xtc->tone(x,y):0;
  if(gray){
   constexpr uint8_t values[]={255,85,170,0};
   assert(r.grayShown[py*r.panelWidth+px]==values[tone]);
  }else{
   bool expected=a.xtc->depth==2?blackFor(tone,x,y):tone>=1;
   bool actual=!(r.shown[py*r.panelWidthBytes+px/8]&(1u<<(7-px%8)));
   assert(actual==expected);
  }
 }
 r.checkGuards();assert(live==0);
}
void resetCounters(){allocations=frees=live=yields=watchdogs=0;clockMs=clockStep=0;allocationFail=loadFail=false;}
int main(){
 for(auto size:{std::pair<unsigned,unsigned>{32,24},{800,480},{960,540}}){
  for(int orientation=0;orientation<4;++orientation)for(bool gray:{false,true}){
   resetCounters();XtcReaderActivity a(size.first,size.second);
   a.renderer.orientation=static_cast<GfxRenderer::Orientation>(orientation);
   a.renderer.grayAvailable=gray;
   // Overdraw verifies source clipping without moving the tone origin.
   a.xtc->w=a.renderer.getScreenWidth()+8;
   a.xtc->h=((a.renderer.getScreenHeight()+15)/8)*8;
   a.renderPage();verify(a,gray);
   assert(allocations==1&&frees==1&&a.renderer.captures==1);
   assert(a.renderer.grayPresents==unsigned(gray)&&a.renderer.monoPresents==unsigned(!gray));
   assert(a.renderer.cleanups==unsigned(gray)&&a.pagesUntilFullRefresh==5);
   if(!gray)assert(yields&&yields==watchdogs);
   const auto prior=gray?a.renderer.grayShown:a.renderer.shown;
   a.renderPage();verify(a,gray);
   assert((gray?a.renderer.grayShown:a.renderer.shown)==prior);
   assert(allocations==2&&frees==2&&a.pagesUntilFullRefresh==4);
   // A failed load must free the only page allocation and permit retry.
   loadFail=true;a.renderPage();assert(!live&&allocations==3&&frees==3&&a.renderer.errors==1);
   loadFail=false;a.renderPage();verify(a,gray);
  }
 }
 // Exact tone densities from the existing dither primitives.
 for(int tone=0;tone<4;++tone){
  resetCounters();XtcReaderActivity a(32,24);a.xtc->w=24;a.xtc->h=32;a.xtc->pattern=tone;
  a.renderPage();verify(a,false);unsigned black=0;
  for(uint8_t byte:a.renderer.shown)black+=8-__builtin_popcount(unsigned(byte));
  constexpr unsigned expected[]={0,384,192,768};
  assert(black==expected[tone]);
 }
 // Existing one-bit page path is byte/pixel-identical under every rotation.
 for(int orientation=0;orientation<4;++orientation){
  resetCounters();XtcReaderActivity a(32,24);a.renderer.orientation=static_cast<GfxRenderer::Orientation>(orientation);
  a.xtc->depth=1;a.xtc->w=a.renderer.getScreenWidth()+3;a.xtc->h=a.renderer.getScreenHeight();
  a.renderPage();verify(a,false);assert(!a.renderer.captures&&allocations==1&&frees==1);
 }
 resetCounters();XtcReaderActivity failed(32,24);
 allocationFail=true;failed.renderPage();assert(!live&&!frees&&!failed.xtc->loads&&failed.renderer.errors==1);
 allocationFail=false;failed.renderPage();verify(failed,false);assert(allocations==2&&frees==1);
 // Less than sixteen rows still cooperates on elapsed time, including wrap.
 for(uint32_t start:{0u,0xfffffff0u}){
  resetCounters();clockMs=start;clockStep=21;XtcReaderActivity a(16,8);
  a.renderer.orientation=GfxRenderer::LandscapeCounterClockwise;a.xtc->w=16;a.xtc->h=8;
  a.renderPage();verify(a,false);assert(yields==8&&watchdogs==8);
 }
 puts("Actual XTH renderPage/pixel/rotation/dither: every pixel, four tones, three panels, all orientations, clipping, gray parity, allocation/read failure, retry, cleanup and rollover yields PASS");
}
'''
with tempfile.TemporaryDirectory(prefix="xtc-render-") as directory:
    cpp=Path(directory)/"test.cpp";binary=Path(directory)/"test"
    cpp.write_text(prefix+production+scaffold+method(reader,"void XtcReaderActivity::renderPage()")+tests)
    flags=['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation']
    if os.environ.get("XTC_RENDER_SANITIZE","1") == "1":
        flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(['c++',*flags,str(cpp),'-o',str(binary)],check=True,timeout=45)
    subprocess.run([str(binary)],check=True,timeout=30,env=os.environ)
