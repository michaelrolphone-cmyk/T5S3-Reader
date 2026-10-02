#include "T5AppApi.h"
#include "T5UiApi.h"
#include "T5CacheApi.h"
#include "T5OtaApi.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
extern "C" void clear_cache_main();
extern "C" void ota_update_main();

struct Rect { int x,y,width,height; Rect(int a,int b,int c,int d):x(a),y(b),width(c),height(d){} };
struct GfxRenderer {
  enum class Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  Orientation orientation = Orientation::Portrait;
  int getDisplayVisibleWidth() const { return 540; }
  int getDisplayVisibleHeight() const { return 960; }
  int getScreenWidth() const { return int(orientation)%2 ? 960 : 540; }
  int getScreenHeight() const { return int(orientation)%2 ? 540 : 960; }
  Orientation getOrientation() const { return orientation; }
} display;
struct MappedInputManager {
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power };
  std::array<Button,4> mapping{Button::Back, Button::Confirm, Button::Left, Button::Right};
  uint32_t pressed=0,released=0;
  bool resolveTouchFrontButton(size_t i, Button& b) { b=mapping.at(i); return true; }
  bool wasPressed(Button b) { return pressed & (1u << unsigned(b)); }
  bool wasReleased(Button b) { return released & (1u << unsigned(b)); }
} controls;
struct ButtonNavigator {
  void onNext(const std::function<void()>& f) { if(controls.wasPressed(MappedInputManager::Button::Right)) f(); }
  void onPrevious(const std::function<void()>& f) { if(controls.wasPressed(MappedInputManager::Button::Left)) f(); }
};
struct Gui {
  std::array<Rect,4> getButtonHintTouchBounds(const GfxRenderer&) {
    return {Rect(0,920,130,40),Rect(135,920,130,40),Rect(270,920,130,40),Rect(405,920,130,40)};
  }
} GUI;
struct HitLayout { int headerBottom=90,rowTop=120,rowHeight=90,pageItems=1,pageStart=0,rowCount=1; } hitLayout;
std::unique_ptr<ButtonNavigator> navigator;
GfxRenderer* renderer() {return &display;}
MappedInputManager* input() {return &controls;}
#include "ui_functions.inc"

struct Frame { t5_app_input_t raw{}; bool ok=true; };
std::vector<Frame> frames;
size_t cursor;
uint32_t previousButtons;
unsigned cleared,installed,restarted,renders,polls;
bool cacheOk=true,newer=true,missingPoll=false;
t5_ota_result_t checkResult=T5_OTA_OK,installResult=T5_OTA_OK;
t5_app_api_v1 core{};
t5_ui_api_v1 ui{};
t5_cache_api_v1 cache{};
t5_ota_api_v1 ota{};
bool rawPoll(t5_app_input_t* out,uint32_t) {
  assert(++polls<200);
  if(cursor>=frames.size()) return false;
  Frame f=frames[cursor++]; *out=f.raw;
  controls.pressed=f.raw.buttons & ~previousButtons;
  controls.released=previousButtons & ~f.raw.buttons;
  previousButtons=f.raw.buttons;
  return f.ok;
}
void render(const t5_ui_chrome_t* chrome,const t5_ui_list_row_t*,uint32_t n,int32_t) {
  assert(n==1); ++renders;
  assert(chrome && chrome->back_label && chrome->confirm_label);
  hitLayout={};
}
bool clear(t5_cache_clear_result_t* r) { ++cleared; r->directory_available=1; r->removed_count=3; return cacheOk; }
t5_ota_result_t check() {return checkResult;}
bool isNewer() {return newer;}
bool latest(char* out,size_t n) {snprintf(out,n,"99.0.0");return true;}
size_t size() {return 100;}
t5_ota_result_t install(t5_ota_progress_callback_t cb,void* ctx) {++installed;cb(ctx);return installResult;}
void restart() {++restarted;}
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t v) {assert(v==T5_APP_ABI_VERSION);return &core;}
extern "C" const t5_ui_api_v1* t5_ui_get_api(uint32_t v) {assert(v==T5_UI_API_VERSION);ui.poll_event=missingPoll?nullptr:pollEvent;return &ui;}
extern "C" const t5_cache_api_v1* t5_cache_get_api(uint32_t v) {assert(v==T5_CACHE_API_VERSION);return &cache;}
extern "C" const t5_ota_api_v1* t5_ota_get_api(uint32_t v) {assert(v==T5_OTA_API_VERSION);return &ota;}
Frame tap(int x,int y) {Frame f;f.raw.tapped=true;f.raw.touch_x=x;f.raw.touch_y=y;return f;}
Frame button(uint32_t b) {Frame f;f.raw.buttons=b;return f;}
Frame exitFrame() {Frame f;f.raw.exit_requested=true;return f;}
Frame hint(MappedInputManager::Button b) {
  for(size_t i=0;i<4;++i) if(controls.mapping[i]==b) {
    Rect r=rotatePortraitRectToCurrentOrientation(GUI.getButtonHintTouchBounds(display)[i],display);
    return tap(r.x+r.width/2,r.y+r.height/2);
  }
  assert(false);return {};
}
void reset() {
  frames.clear();cursor=0;previousButtons=0;controls.pressed=controls.released=0;
  cleared=installed=restarted=renders=polls=0;cacheOk=newer=true;missingPoll=false;
  checkResult=installResult=T5_OTA_OK;hitLayout={};
  core={};core.poll=rawPoll;ui={};ui.render_list=render;ui.hit_test=hitTest;ui.poll_event=pollEvent;
  cache={};cache.clear_reading_cache=clear;
  ota={};ota.check_for_update=check;ota.is_update_newer=isNewer;ota.latest_version=latest;
  ota.processed_size=ota.total_size=size;ota.install_update=install;ota.restart_after_update=restart;
}
void invoke(bool isOta,unsigned expected) {
  if(isOta) ota_update_main();else clear_cache_main();
  if(cleared!=(isOta?0:expected)||installed!=(isOta?expected:0)) fprintf(stderr,"Failure ota=%d orientation=%d expected=%u cleared=%u installed=%u polls=%u first=(%d,%d)\n",isOta,int(display.orientation),expected,cleared,installed,polls,frames.empty()?0:frames[0].raw.touch_x,frames.empty()?0:frames[0].raw.touch_y);
  assert(cleared==(isOta?0:expected)); assert(installed==(isOta?expected:0));
  assert(restarted==(isOta && installResult==T5_OTA_OK?expected:0));
}
int main(int argc,char** argv) {
  const bool onlyCache=argc>1 && !strcmp(argv[1],"cache");
  const bool onlyOta=argc>1 && !strcmp(argv[1],"ota");
  unsigned cases=0;
  for(bool isOta:{false,true}) {
    if((isOta && onlyCache)||(!isOta && onlyOta))continue;
    for(int orientation=0;orientation<4;++orientation) {
      display.orientation=GfxRenderer::Orientation(orientation);
      controls.mapping={MappedInputManager::Button::Back,MappedInputManager::Button::Confirm,
                        MappedInputManager::Button::Left,MappedInputManager::Button::Right};
      do {
        // Body row, header, whitespace and off-screen taps cannot authorize.
        for(auto p: {std::array<int,2>{display.getScreenWidth()/2,150}, {20,20},
                     {display.getScreenWidth()/2,400}, {-1,-1}, {display.getScreenWidth(),150}}) {
          bool onHint=false;
          for(const auto& b:GUI.getButtonHintTouchBounds(display))
            onHint=onHint || containsPoint(rotatePortraitRectToCurrentOrientation(b,display),p[0],p[1]);
          if(onHint) continue; // A rotated action control is not body content.
          reset();frames={tap(p[0],p[1]),tap(p[0],p[1]),exitFrame()};invoke(isOta,0);++cases;
        }
        reset();frames={hint(MappedInputManager::Button::Confirm),exitFrame()};invoke(isOta,1);++cases;
        reset();frames={hint(MappedInputManager::Button::Back),hint(MappedInputManager::Button::Confirm)};invoke(isOta,0);++cases;
        reset();frames={hint(MappedInputManager::Button::Left),hint(MappedInputManager::Button::Right),exitFrame()};invoke(isOta,0);++cases;
        // Held Confirm acts once on release; level-only samples never authorize.
        reset();frames={button(T5_APP_BUTTON_CONFIRM),button(T5_APP_BUTTON_CONFIRM),exitFrame()};invoke(isOta,0);++cases;
        reset();frames={button(T5_APP_BUTTON_CONFIRM),button(T5_APP_BUTTON_CONFIRM),button(0),
                        button(T5_APP_BUTTON_CONFIRM),button(0),exitFrame()};invoke(isOta,1);++cases;
        reset();frames={button(T5_APP_BUTTON_BACK),hint(MappedInputManager::Button::Confirm)};invoke(isOta,0);++cases;
        reset();frames={exitFrame(),hint(MappedInputManager::Button::Confirm)};invoke(isOta,0);++cases;
        reset();Frame failed;failed.ok=false;frames={failed,hint(MappedInputManager::Button::Confirm)};invoke(isOta,0);++cases;
        // Failure/result screens and reopening never execute a second action.
        reset();cacheOk=false;installResult=T5_OTA_HTTP_ERROR;
        frames={hint(MappedInputManager::Button::Confirm),hint(MappedInputManager::Button::Confirm),exitFrame()};invoke(isOta,1);++cases;
        reset();frames={exitFrame()};invoke(isOta,0);++cases;
      } while(std::next_permutation(controls.mapping.begin(),controls.mapping.end()));
    }
    reset();missingPoll=true;frames={tap(300,150)};invoke(isOta,0);assert(polls==0);++cases;
    if(isOta) {
      reset();checkResult=T5_OTA_HTTP_ERROR;frames={button(T5_APP_BUTTON_CONFIRM)};invoke(true,0);++cases;
      reset();newer=false;frames={button(T5_APP_BUTTON_CONFIRM)};invoke(true,0);++cases;
    }
  }
  printf("Native confirmation runtime PASS (%u cases)\n",cases);
}
