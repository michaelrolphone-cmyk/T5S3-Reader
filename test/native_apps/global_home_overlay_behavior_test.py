#!/usr/bin/env python3
"""Execute production ELF Home/modal paths with deterministic input and framebuffer."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
host = (ROOT/'src/native/NativeAppHost.cpp').read_text()
menu = (ROOT/'src/activities/GlobalMenuActivity.cpp').read_text()
poll = host[host.index('bool pollInput('):host.index('\nbool poll(t5_app_input_t*')]
current = host[host.index('Session* current()'):host.index('void beginAppInput(')]
modal = menu[menu.index('GlobalMenuActivity::ModalResult GlobalMenuActivity::runFirmwareModal('):menu.index('\nvoid GlobalMenuActivity::render(RenderLock&&)')]
confirm = menu[menu.index('bool modalShutdownConfirmed('):menu.index('\n}  // namespace')]
prefix = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
static uint32_t nowMs;
static unsigned ticks;
unsigned long millis() { return nowMs; }
void delay(unsigned n) { nowMs+=n; assert(++ticks<100); }
void esp_task_wdt_reset() {}
enum class DisplayPresentMode { Quality, Clean, LowLatency };
enum class EpdFontFamily { BOLD, REGULAR };
enum class StrId { STR_SHUTDOWN, STR_SHUTDOWN_PROMPT, STR_CANCEL, STR_CONFIRM };
constexpr int UI_10_FONT_ID=10;
struct { const char* get(StrId) { return "text"; } } I18N;
struct { bool doubleClickHomeMenu=true, confirmShutdown=true; int backlightLevel=5; } SETTINGS;
bool shutdownRequested=false, takeover=false;
void requestShutdown() { shutdownRequested=true; }
bool nativeHardwareTakeoverDisplayActive() { return takeover; }
bool hasBacklight() { return false; }
struct GfxRenderer {
  uint8_t frame[64]; std::vector<std::vector<uint8_t>> frames;
  GfxRenderer() { std::memset(frame,0x42,sizeof(frame)); }
  size_t getBufferSize() { return sizeof(frame); }
  uint8_t* getFrameBuffer() { return frame; }
  void displayBuffer(DisplayPresentMode) { frames.emplace_back(frame,frame+64); }
  void clearScreen() { std::memset(frame,0,sizeof(frame)); }
  int getLineHeight(int) { return 10; }
  int getScreenWidth() { return 100; }
  int getScreenHeight() { return 100; }
  std::string truncatedText(int,const char* s,int,EpdFontFamily) { return s; }
  void drawCenteredText(int,int,const char*,bool,EpdFontFamily) { frame[32]=7; }
};
namespace RuntimeMemory {
static bool failSnapshot;
struct PsramBuffer {
  std::vector<uint8_t> bytes;
  PsramBuffer(size_t n,bool) : bytes(failSnapshot?0:n) {}
  explicit operator bool() { return !bytes.empty(); }
  uint8_t* data() { return bytes.data(); }
};
}
struct MappedInputManager {
 enum class Button {Back,Confirm,Left,Right,Up,Down,Power};
 struct TouchPoint {int x=0,y=0;};
 struct Labels {const char *btn1,*btn2,*btn3,*btn4;};
 struct Event {int release=-1; bool home=false,tap=false; int x=0,y=0;};
 std::deque<unsigned long> homes;
 std::deque<Event> script;
 Event frame{};
 bool power=false;
 void update() { frame={}; if(!script.empty()){frame=script.front();script.pop_front();} }
 bool wasReleased(Button b) {return frame.release==int(b);}
 bool isPressed(Button b) {return b==Button::Power&&power;}
 bool wasAnyPressed(){return power || frame.home || frame.tap;}
 bool wasAnyReleased(){return frame.release>=0;}
 bool wasTouchTapped(TouchPoint& p,GfxRenderer&) {p={frame.x,frame.y};return frame.tap;}
 bool wasTouchHomeButtonPressed() {bool h=frame.home;frame.home=false;return h;}
 bool takeTouchHomeButtonPress(unsigned long& t) {if(homes.empty())return false;t=homes.front();homes.pop_front();return true;}
 Labels mapLabels(const char*,const char*,const char*,const char*) {return {"","","",""};}
};
struct { void drawButtonHints(GfxRenderer&,const char*,const char*,const char*,const char*) {} } GUI;
struct ButtonNavigator {
 static int nextIndex(int x,int n){return (x+1)%n;}
 static int previousIndex(int x,int n){return (x+n-1)%n;}
 template<class F> void onPressAndContinuous(std::initializer_list<MappedInputManager::Button>,F) {}
 template<class F> void onNext(F) {}
 template<class F> void onPrevious(F) {}
};
struct GlobalMenuActivity {
 enum class ModalResult {Dismissed,ShutdownRequested,Unavailable};
 enum {BUTTON_BACKLIGHT=0,BUTTON_SHUTDOWN=1,BUTTON_COUNT=2};
 GfxRenderer& renderer; int selectedIndex=0; ButtonNavigator buttonNavigator;
 GlobalMenuActivity(GfxRenderer& r,MappedInputManager&,bool):renderer(r){}
 void renderOverlay(DisplayPresentMode m){std::memset(renderer.frame,9,16);renderer.displayBuffer(m);}
 void getPanelLayout(int& x,int& y,int& w,int& h){x=y=0;w=h=50;}
 void getButtonRect(int,int& x,int& y,int& w,int& h){getPanelLayout(x,y,w,h);}
 void applyBacklightLevel(int v,bool){SETTINGS.backlightLevel=v;}
 bool resolveTouchButtonHint(int16_t x,int16_t y,MappedInputManager::Button& b) {
   if(y<80 || y>=100) return false;
   b=x<50?MappedInputManager::Button::Left:MappedInputManager::Button::Right;
   return true;
 }
 static ModalResult runFirmwareModal(GfxRenderer&,MappedInputManager&);
};
struct t5_app_input_t {unsigned buttons=0;bool tapped=false;int touch_x=0,touch_y=0;bool exit_requested=false;};
static int xTaskGetCurrentTaskHandle(){return 1;}
struct Session {std::string launchPath;int owner=1;MappedInputManager input;GfxRenderer renderer;bool backExitsApp=false,exiting=false,presenting=false,pendingHomeSingle=false;unsigned long lastHomeEventMs=0;};
Session* session; bool homeRequested=false;
constexpr unsigned long kNativeHomeDoubleClickWindowMs=400;

void beginAppInput(Session&) {}
void nativeProviderOwnerTick() {}
bool serviceIdleSleep(bool){return false;} // These cases cover Home/modal routing; idle has a separate actual-host fixture.
struct {unsigned buttons=0;} navigationFrame;
auto& nativeNavigationFrame(){return navigationFrame;}
bool nativeTouchHadActivity(){return false;}
'''
tests = r'''
using B=MappedInputManager::Button;
void reset(Session& s){s=Session{};session=&s;nowMs=1000;ticks=0;homeRequested=shutdownRequested=takeover=false;RuntimeMemory::failSnapshot=false;SETTINGS.doubleClickHomeMenu=SETTINGS.confirmShutdown=true;}
void homeDismiss(Session& s){s.input.script={{},{-1,true}};}
void appFrame(const std::vector<uint8_t>& f){for(auto x:f)assert(x==0x42);}
int main(){
 Session s; t5_app_input_t out;
 // Delayed delivery: both captured taps predate polling by seconds.
 reset(s);s.input.homes={100,450};homeDismiss(s);assert(pollInput(&out,0,false));
 assert(!out.exit_requested&&!homeRequested&&s.renderer.frames.size()==2);appFrame(s.renderer.frames.back());
 // Boundary: 400 ms is double; 401 ms is two singles, never an overlay.
 reset(s);s.input.homes={100,500};homeDismiss(s);pollInput(&out,0,false);assert(!out.exit_requested);
 reset(s);s.input.homes={100,501};pollInput(&out,0,false);assert(out.exit_requested&&homeRequested&&s.renderer.frames.empty());
 // Single waits through the window; delayed single immediately exits.
 reset(s);nowMs=100;s.input.homes={100};pollInput(&out,0,false);assert(!out.exit_requested);
 nowMs=500;pollInput(&out,0,false);assert(!out.exit_requested);
 nowMs=501;pollInput(&out,0,false);assert(out.exit_requested&&homeRequested);
 reset(s);s.input.homes={100};pollInput(&out,0,false);assert(out.exit_requested);
 // ESP unsigned clock rollover; host unsigned long is wider, set equivalent wrap.
 reset(s);s.input.homes={~0UL-99,100};homeDismiss(s);pollInput(&out,0,false);assert(!out.exit_requested);
 reset(s);takeover=true;s.input.homes={990};pollInput(&out,0,false);assert(out.exit_requested&&homeRequested&&s.renderer.frames.empty());
 reset(s);SETTINGS.doubleClickHomeMenu=false;s.input.homes={990};pollInput(&out,0,false);assert(out.exit_requested&&homeRequested);
 reset(s);RuntimeMemory::failSnapshot=true;s.input.homes={100,300};pollInput(&out,0,false);assert(out.exit_requested&&homeRequested&&s.renderer.frames.empty());
 // No modal may touch the framebuffer during a serviced refresh.
 reset(s);s.presenting=true;s.input.homes={100,300};assert(!pollInput(&out,0,false));assert(s.input.homes.size()==2&&s.renderer.frames.empty());
 s.presenting=false;homeDismiss(s);pollInput(&out,0,false);assert(!out.exit_requested&&s.input.homes.empty());
 // Cancel shutdown after full-screen confirmation: restore app below menu.
 reset(s);s.input.homes={100,300};s.input.script={{},{int(B::Confirm)},{int(B::Left)},{-1,true}};
 pollInput(&out,0,false);assert(!out.exit_requested&&!shutdownRequested&&s.renderer.frames.size()==4);
 for(size_t i=16;i<64;++i)assert(s.renderer.frames[2][i]==0x42);appFrame(s.renderer.frames[3]);
 // Confirm requests shutdown and unwinds Springboard so main can power off.
 reset(s);s.input.homes={100,300};s.input.script={{},{int(B::Confirm)},{int(B::Right)}};
 pollInput(&out,0,false);assert(out.exit_requested&&shutdownRequested&&homeRequested);
 reset(s);SETTINGS.confirmShutdown=false;s.input.homes={100,300};s.input.script={{},{int(B::Confirm)}};
 pollInput(&out,0,false);assert(out.exit_requested&&shutdownRequested);
 // Embedded shutdown modal ignores right-half body taps, but accepts the mapped Confirm hint.
 reset(s);GlobalMenuActivity menu(s.renderer,s.input,false);s.input.script={{-1,false,true,75,65},{int(B::Left)}};
 assert(!modalShutdownConfirmed(menu,s.renderer,s.input)&&!shutdownRequested);
 reset(s);GlobalMenuActivity confirmMenu(s.renderer,s.input,false);s.input.script={{-1,false,true,75,90}};
 assert(modalShutdownConfirmed(confirmMenu,s.renderer,s.input));
 // Outside-panel tap and Back dismissal restore the complete original frame.
 reset(s);s.input.script={{-1,false,true,90,90}};assert(GlobalMenuActivity::runFirmwareModal(s.renderer,s.input)==GlobalMenuActivity::ModalResult::Dismissed);appFrame(s.renderer.frames.back());
 reset(s);s.input.script={{int(B::Back)}};assert(GlobalMenuActivity::runFirmwareModal(s.renderer,s.input)==GlobalMenuActivity::ModalResult::Dismissed);appFrame(s.renderer.frames.back());
 puts("production Home/modal timing, takeover, refresh exclusion, framebuffer and shutdown behavior PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    d=Path(d); cpp=d/'home.cpp'; binary=d/'home'
    cpp.write_text(prefix+current+confirm+'\n'+modal+'\n'+poll+tests)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-fsanitize=address,undefined',str(cpp),'-o',str(binary)],check=True,timeout=60)
    subprocess.run([str(binary)],check=True,timeout=30)
