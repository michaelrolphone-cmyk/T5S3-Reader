#!/usr/bin/env python3
"""Compile real X4 display and shared Settings/touch methods for flip parity."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--bridge-source-ref', help='Read baseline bridge methods for negative reproduction')
args = parser.parse_args()


def source(path):
    if args.bridge_source_ref:
        return subprocess.check_output(['git', 'show', args.bridge_source_ref + ':' + path], cwd=ROOT, text=True)
    return (ROOT / path).read_text()


def run(build, name, text, includes=(), sources=(), defines=()):
    cpp = build / (name + '.cpp')
    cpp.write_text(text)
    binary = build / name
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    *['-D' + value for value in defines], *['-I' + str(p) for p in includes],
                    str(cpp), *[str(p) for p in sources], '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)


with tempfile.TemporaryDirectory() as temp:
    build = Path(temp)
    (build / 'Arduino.h').write_text('#pragma once\n#include <cstdint>\n#include <cstddef>\n#include <cstring>\n')
    (build / 'Logging.h').write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n')
    (build / 'Board.h').write_text('''#pragma once
namespace BoardPins {
constexpr unsigned DisplayWidth=800, DisplayHeight=480, LogicalWidth=480, LogicalHeight=800;
}
''')
    if not args.bridge_source_ref:
        run(build, 'x4-display', r'''
#include <HalDisplay.h>
#include "runtime/display/ProviderDisplaySurface.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static uint8_t raster[48000], pixels[48000], original[48000];
static unsigned presents;
static uint8_t lastIntent;
static bool presentOkay=true, suppressed=false;
static unsigned suppressionCalls,queuedTouches;
void nativeTouchSuppressCoordinates(bool value){suppressed=value;++suppressionCalls;queuedTouches=0;}
static bool info(void*, risc_display_info_v1* out) {
  *out={}; out->api_version=1;out->struct_size=sizeof(*out);
  out->width=800;out->height=480;out->preferred_format=RISC_DISPLAY_FORMAT_MONO1;
  out->supported_formats=RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1);return true;
}
static bool acquire(void*, uint32_t, risc_display_surface_v1* out) {
  *out={1,pixels,800,480,100,sizeof(pixels),RISC_DISPLAY_FORMAT_MONO1};return true;
}
static void release(void*, risc_display_frame_v1) {}
static bool submit(void*, risc_display_frame_v1, const risc_display_rect_v1*, size_t,
                   const risc_display_present_options_v1* options,risc_display_present_token_v1* token) {
  ++presents;lastIntent=options->intent;*token=1;return true;
}
static bool wait(void*, risc_display_present_token_v1, uint32_t, risc_display_present_status_v1* out) {
  *out={};queuedTouches=3;out->state=presentOkay?RISC_DISPLAY_PRESENT_COMPLETE:RISC_DISPLAY_PRESENT_FAILED;return presentOkay;
}
static void check(bool flipped) {
  for(unsigned y=0;y<480;++y) for(unsigned x=0;x<800;++x) {
    const unsigned sx=flipped?799-x:x,sy=flipped?479-y:y;
    const bool black=!(raster[sy*100+sx/8]&(0x80u>>(sx%8)));
    assert(bool(pixels[y*100+x/8]&(0x80u>>(x%8)))==black);
  }
  assert(!std::memcmp(raster,original,sizeof(raster)));
}
int main() {
  risc_display_output_api_v1 api{};api.api_version=1;api.struct_size=sizeof(api);
  api.get_info=info;api.acquire=acquire;api.release=release;api.submit=submit;api.wait_present=wait;
  ProviderDisplaySurface surface(&api,raster,sizeof(raster),480,800);
  for(size_t i=0;i<sizeof(raster);++i)raster[i]=static_cast<uint8_t>(i*29u+i/100u);
  std::memcpy(original,raster,sizeof(raster));
  display.setFlipOutput(true); // Saved preference may be set before attachment.
  assert(suppressed&&suppressionCalls==1);
  display.displayBuffer(HalDisplay::FAST_REFRESH);assert(suppressed&&presents==0);
  assert(display.attachProvider(surface));display.begin(false);
  assert(display.isReady()&&display.getFrameBuffer()==raster);
  assert(display.getSurfaceInfo().visibleWidth==480);
  presentOkay=false;
  display.displayBuffer(HalDisplay::FAST_REFRESH);assert(suppressed&&suppressionCalls==1);
  // App/Settings exit does not own this boundary; an eventual Home retry does.
  presentOkay=true;display.displayBuffer(HalDisplay::FAST_REFRESH);check(true);
  assert(!suppressed&&!queuedTouches&&suppressionCalls==2);
  assert(lastIntent==RISC_DISPLAY_PRESENT_CLEAN&&presents==2);
  display.setFlipOutput(true);display.displayBuffer(HalDisplay::FAST_REFRESH);check(true);
  assert(lastIntent==RISC_DISPLAY_PRESENT_LOW_LATENCY&&suppressionCalls==2);
  display.setFlipOutput(false);display.displayBuffer(HalDisplay::FAST_REFRESH);check(false);
  assert(lastIntent==RISC_DISPLAY_PRESENT_CLEAN);
  display.displayBuffer(HalDisplay::FAST_REFRESH);check(false);
  assert(lastIntent==RISC_DISPLAY_PRESENT_LOW_LATENCY);
  puts("Actual X4 HAL: saved/live flip, unchanged raster, full 800x480 pixel mapping PASS");
}
''', (build, ROOT / 'lib/hal', ROOT / 'lib/DisplaySurface', ROOT / 'src', ROOT / 'sdk/driver'),
            (ROOT / 'lib/hal/HalDisplayX4.cpp',), ('BOARD_XTEINK_X4_PRO',))

    if not args.bridge_source_ref:
        t5 = (ROOT / 'lib/hal/HalDisplay.cpp').read_text()
        present = t5[t5.index('void HalDisplay::pushPanelCanvas('):t5.index('void HalDisplay::displayBufferDiff(')]
        setter = t5[t5.index('void HalDisplay::setFlipOutput('):t5.index('void HalDisplay::requestNextRefresh(')]
        run(build, 't5-display', r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
static bool suppressed=false;
static unsigned suppressionCalls,queuedTouches;
void nativeTouchSuppressCoordinates(bool value){suppressed=value;++suppressionCalls;queuedTouches=0;}
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
namespace lgfx { namespace epd_mode { enum epd_mode_t {epd_fastest,epd_quality,epd_text,epd_fast}; } }
constexpr unsigned kQualityRefreshThreshold=18,kMiddleRefreshThreshold=8;
struct Panel {
  bool failure=false;
  void waitDisplay(){queuedTouches=3;}
  void setEpdMode(lgfx::epd_mode::epd_mode_t){}
  bool failed()const{return failure;}
} panel;
struct Canvas {void pushSprite(int,int){}} canvas;
struct HalDisplay {
  enum RefreshMode {FULL_REFRESH,HALF_REFRESH,BALANCED_REFRESH,FAST_REFRESH};
  enum DisplayEffect {EFFECT_NONE};
  bool flipOutput=false,flipTouchBoundaryPending=false,displayReady=true,externalOwner=false;
  bool forceFullRefresh=false,forcedRefreshPending=false,grayscaleBaseCaptured=false;
  unsigned refreshCycleCount=0;
  RefreshMode forcedRefreshMode=FULL_REFRESH;
  DisplayEffect pendingDisplayEffect=EFFECT_NONE;
  uint8_t raster[1]{};uint8_t*frameBuffer=raster;
  Panel*gfx=&panel;Canvas*panelCanvas=&canvas;
  void setFlipOutput(bool);
  void pushPanelCanvas(RefreshMode,lgfx::epd_mode::epd_mode_t);
  void displayBuffer(RefreshMode,bool=false);
  void renderBwToPanelCanvas(){}
  void pushPanelCanvasWithEffect(DisplayEffect){}
};
''' + present + setter + r'''
int main(){
  HalDisplay display;
  display.setFlipOutput(true);assert(suppressed&&display.forceFullRefresh);
  panel.failure=true;display.displayBuffer(HalDisplay::FAST_REFRESH);
  assert(suppressed&&!display.displayReady&&display.forceFullRefresh&&display.flipTouchBoundaryPending);
  panel.failure=false;display.displayBuffer(HalDisplay::FAST_REFRESH);
  assert(suppressed&&suppressionCalls==1); // Skipped present cannot release.
  display.displayReady=true; // Successful backend recovery permits a new present.
  display.displayBuffer(HalDisplay::FAST_REFRESH);
  assert(!suppressed&&!queuedTouches&&!display.forceFullRefresh&&!display.flipTouchBoundaryPending);
  assert(suppressionCalls==2);
  display.setFlipOutput(true);assert(suppressionCalls==2);
  display.setFlipOutput(false);assert(suppressed&&display.forceFullRefresh);
  display.displayBuffer(HalDisplay::FAST_REFRESH);assert(!suppressed&&!queuedTouches);
  puts("Actual T5 HAL: failed/skipped present retains flip fence and Clean, successful retry releases PASS");
}
''')

    bridge = source('src/native/NativeSettingsBridge.cpp')
    methods = bridge[bridge.index('uint8_t nativeSettingsActivate('):bridge.index('uint8_t nativeSettingsTouch(')]
    mapped = source('src/MappedInputManager.cpp')
    touch = mapped[mapped.index('MappedInputManager::TouchPoint orientTouchPoint('):mapped.index('}  // namespace')]
    fixture = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <DisplaySurface.h>
#include <T5AppApi.h>
static std::string events;
static unsigned queuedTouches=7,discardCalls=0,saveCalls=0,flipCalls=0;
static bool suppressed=false,presentOkay=true;
struct CrossPointSettings {uint8_t flipUi=0,backlightLevel=0,other=0;void saveToFile(){++saveCalls;events+='S';}} SETTINGS;
void nativeTouchDiscardGestures(){queuedTouches=0;++discardCalls;events+='D';}
// Model HAL ownership: only successful presentation releases suppression.
struct Display {bool flipped=false;void setFlipOutput(bool value){flipped=value;suppressed=true;++flipCalls;events+='F';}} display;
namespace Board {void setBacklightLevel(uint8_t){events+='B';}}
enum class StrId {STR_FONT_FAMILY,STR_SETTINGS_TITLE,STR_TOGGLE,STR_BACK,STR_DIR_UP,STR_DIR_DOWN};
struct {const char*get(StrId){return "test";}} I18N;
enum class SettingType {TOGGLE,ENUM,VALUE,TIMEZONE,ACTION};
struct SettingInfo {
  SettingType type=SettingType::TOGGLE;uint8_t CrossPointSettings::*valuePtr=nullptr;
  std::vector<int>enumValues;std::function<uint8_t()>valueGetter;std::function<void(uint8_t)>valueSetter;
  std::vector<std::string>enumStringValues;StrId nameId=StrId::STR_SETTINGS_TITLE;
  struct {uint8_t min=0,max=10,step=1;}valueRange;int action=0;
};
enum class PendingAction {None,FontSelection,TimeZone};
static constexpr uint32_t kCategoryCount=4;
static std::array<StrId,4> kCategoryNames{};
uint8_t requestAction(PendingAction){return T5_APP_SETTING_ACTION_REQUESTED;}
PendingAction mapAction(int){return PendingAction::None;}
void buildSettings(){}
std::string settingValue(const SettingInfo&){return "value";}
struct Rect{int x,y,width,height;};
struct TabInfo{const char*label;bool selected;};
struct GfxRenderer {
  enum Orientation {Portrait,LandscapeClockwise,PortraitInverted,LandscapeCounterClockwise};
  Orientation orientation=Portrait;
  Orientation getOrientation()const{return orientation;}
  int getScreenWidth()const{return orientation==Portrait||orientation==PortraitInverted?480:800;}
  int getScreenHeight()const{return orientation==Portrait||orientation==PortraitInverted?800:480;}
  void clearScreen(){}
  void displayBuffer(DisplayPresentMode){events+='P';queuedTouches=3;if(presentOkay){suppressed=false;queuedTouches=0;}}
} renderer;
struct NativeTouchPoint{uint16_t x,y;};
struct MappedInputManager {
  struct TouchPoint{int16_t x,y;};
  struct Labels{const char*btn1;const char*btn2;const char*btn3;const char*btn4;};
  Labels mapLabels(const char*a,const char*b,const char*c,const char*d){return {a,b,c,d};}
} input;
struct UITheme {
  struct Metrics{int topPadding=0,headerHeight=30,tabBarHeight=25,verticalSpacing=5,buttonHintsHeight=30;} metrics;
  static UITheme&getInstance(){static UITheme theme;return theme;}
  const Metrics&getMetrics(){return metrics;}
};
struct Gui {
  template<class...T>void drawHeader(T...){}
  template<class...T>void drawTabBar(T...){}
  template<class...T>void drawList(T...){}
  template<class...T>void drawButtonHints(T...){}
} GUI;
struct {GfxRenderer*renderer=&::renderer;MappedInputManager*input=&::input;
  std::array<std::vector<SettingInfo>,4>settings;bool flipTouchBoundaryPending=false;} state;
#define CROSSPOINT_VERSION "test"
'''
    checks = r'''
int main(){
  SettingInfo flip;flip.valuePtr=&CrossPointSettings::flipUi;
  SettingInfo backlight;backlight.type=SettingType::VALUE;backlight.valuePtr=&CrossPointSettings::backlightLevel;
  state.settings[0]={flip,backlight};
  assert(nativeSettingsActivate(0,0)==T5_APP_SETTING_UPDATED);
  assert(display.flipped&&SETTINGS.flipUi==1&&flipCalls==1&&discardCalls==1&&queuedTouches==0);
  assert(events=="DFS"&&state.flipTouchBoundaryPending);
  presentOkay=false;nativeSettingsRender(0,1);
  assert(suppressed&&!state.flipTouchBoundaryPending&&!queuedTouches);
  queuedTouches=2; // Captured after the failed frame; getters remain suppressed.
  events="DFS";presentOkay=true;
  nativeSettingsRender(0,1);
  assert(!suppressed&&!queuedTouches);
  // Only the first attempt needs the shared one-shot fence. HAL owns retries.
  assert(events=="DFSP"&&discardCalls==2);
  assert(!state.flipTouchBoundaryPending&&queuedTouches==0&&discardCalls==2);
  events.clear();nativeSettingsRender(0,1);assert(events=="P"&&discardCalls==2);
  events.clear();assert(nativeSettingsActivate(0,0)==T5_APP_SETTING_UPDATED);
  nativeSettingsRender(0,1);assert(!display.flipped&&!SETTINGS.flipUi&&events=="DFSPD");
  events.clear();assert(nativeSettingsActivate(0,1)==T5_APP_SETTING_UPDATED);
  nativeSettingsRender(0,2);assert(events=="BSP"&&flipCalls==2&&discardCalls==4);
  events.clear();assert(nativeSettingsActivate(99,0)==T5_APP_SETTING_ERROR);assert(events.empty());
  // Known portrait contact, four renderer orientations, each output-flip state.
  const int expected[][2]={{123,456},{343,123},{356,343},{456,356}};
  for(unsigned rotation=0;rotation<4;++rotation){
    renderer.orientation=static_cast<GfxRenderer::Orientation>(rotation);
    for(bool flipFlag:{false,true}){
      SETTINGS.flipUi=flipFlag;const auto point=orientTouchPoint({123,456},renderer);
      assert(point.x==(flipFlag?renderer.getScreenWidth()-1-expected[rotation][0]:expected[rotation][0]));
      assert(point.y==(flipFlag?renderer.getScreenHeight()-1-expected[rotation][1]:expected[rotation][1]));
    }
  }
  puts("Actual shared Settings/touch: immediate flip, stale/transition touch discard, repeated toggle and all orientations PASS");
}
'''
    for board in ('BOARD_XTEINK_X4_PRO', 'BOARD_T5S3_PRO'):
        run(build, 'settings-' + board, fixture + methods + touch + checks,
            (ROOT / 'lib/DisplaySurface', ROOT / 'lib/NativeApps/include'), defines=(board,))
