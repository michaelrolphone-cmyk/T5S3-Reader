
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>
#include <EpdFontFamily.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
#include "Utf8.h"
#include <T5UiApi.h>
#include "native/NativeTextLayoutCache.h"
uint32_t testClock=0;
uint32_t millis(){return testClock;}
void vTaskDelay(unsigned ticks){assert(ticks==1);testClock+=ticks;}
NativeTextLayoutCache textViewCache;
struct Counts { size_t measures=0, measuredBytes=0, allocations=0, allocatedBytes=0, paragraphs=0, drawnLines=0, drawnBytes=0; };
Counts counts; bool countAllocation=false;
void* operator new(size_t n){if(countAllocation){++counts.allocations; counts.allocatedBytes+=n;}if(auto*p=std::malloc(n?n:1))return p; throw std::bad_alloc();}
void operator delete(void*p) noexcept{std::free(p);}
void operator delete(void*p,size_t) noexcept{std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}
void* operator new(size_t n,const std::nothrow_t&) noexcept{
  if(countAllocation){++counts.allocations;counts.allocatedBytes+=n;}
  return std::malloc(n?n:1);
}
void* operator new[](size_t n,const std::nothrow_t& tag) noexcept{return ::operator new(n,tag);}
void operator delete(void*p,const std::nothrow_t&) noexcept{std::free(p);}
void operator delete[](void*p,const std::nothrow_t&) noexcept{std::free(p);}
void operator delete[](void*p) noexcept{std::free(p);}
void operator delete[](void*p,size_t) noexcept{std::free(p);}
#define LOG_ERR(...) ((void)0)
struct Settings {char sdFontFamilyName[32]{};int getUserContentFontId()const{return 0;}} SETTINGS;
uint64_t drawHash=0;
char frameRecord[32768]; size_t frameUsed=0;
class GfxRenderer {
 public:
  int width=600,height=800;
  std::map<int,EpdFontFamily> fontMap;
  bool isSdCardFont(int)const{return false;}
  const auto& getFontMap() const {return fontMap;}
  uint64_t getFontLayoutGeneration()const{return 1;}
  bool getTruncationPrefix(int,const std::string&,int,size_t&,EpdFontFamily::Style)const;
  void ensureSdCardFontReady(int,const char*,uint8_t)const{assert(false);}
  int getTextWidth(int,const char*,EpdFontFamily::Style=EpdFontFamily::REGULAR)const;
  int getTextAdvanceX(int,const char*,EpdFontFamily::Style)const{assert(false);return 0;}
  int getLineHeight(int f)const{return fontMap.at(f).getData()->advanceY;}
  int getScreenWidth()const{return width;}
  int getScreenHeight()const{return height;}
  void getOrientedViewableTRBL(int*a,int*b,int*c,int*d)const{*a=*b=*c=*d=0;}
  void clearScreen(){frameUsed=0;drawHash=1469598103934665603ull;}
  void drawText(int font,int x,int y,const char*s,bool ink,EpdFontFamily::Style style)const{
    const int written=std::snprintf(frameRecord+frameUsed,sizeof(frameRecord)-frameUsed,"DRAW %d %d %d %d %u %s\n",font,x,y,ink,unsigned(style),s);
    assert(written>=0&&size_t(written)<sizeof(frameRecord)-frameUsed);frameUsed+=size_t(written);
    ++counts.drawnLines;counts.drawnBytes+=strlen(s);drawHash^=(x*8192+y);drawHash*=1099511628211ull;
    while(*s){drawHash^=static_cast<unsigned char>(*s++);drawHash*=1099511628211ull;}
  }
};
struct UITheme {static UITheme& getInstance(){static UITheme t;return t;} const ThemeMetrics& getMetrics()const{return BaseMetrics::values;}};
struct MappedInputManager{};
GfxRenderer globalRenderer; MappedInputManager globalInput;
GfxRenderer* renderer(){return &globalRenderer;}
MappedInputManager* input(){return &globalInput;}
const char* safe(const char*s){return s?s:"";}
void drawChrome(GfxRenderer&,MappedInputManager&,const t5_ui_chrome_t*){}
bool presentNativeAppUiFrame(){return true;}
struct HitLayout {int headerBottom=0,rowTop=0,rowHeight=0,pageStart=0,pageItems=0,rowCount=0;} hitLayout;
