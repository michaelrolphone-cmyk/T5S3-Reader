"""Exercise the real native-host first-poll fence: old taps drop, new taps survive."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/native/NativeAppHost.cpp').read_text()
helper=source[source.index('void beginAppInput('):source.index('int32_t width()')]
poll=source[source.index('bool pollInput('):source.index('bool poll(t5_app_input_t*')]
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <T5AppApi.h>
static unsigned boundaries=0, taps=1, ownerTicks=0;
void nativeProviderOwnerTick(){++ownerTicks;}
void nativeTouchDiscardGestures(){++boundaries;taps=0;}
void esp_task_wdt_reset(){}
void delay(unsigned){}
unsigned long millis(){return 0;}
constexpr unsigned long kNativeHomeDoubleClickWindowMs=400;
struct {bool doubleClickHomeMenu=true;} SETTINGS;
static bool requestIdle=false;
bool serviceIdleSleep(bool){return requestIdle;}
struct {unsigned buttons=0;} navigationFrame;
auto& nativeNavigationFrame(){return navigationFrame;}
bool nativeTouchHadActivity(){return false;}
bool nativeHardwareTakeoverDisplayActive(){return false;}
struct MappedInputManager {
 enum class Button{Back,Confirm,Left,Right,Up,Down,Power};
 struct TouchPoint{int16_t x=0,y=0;};
 void update(){}
 bool wasAnyPressed(){return false;}
 bool wasAnyReleased(){return false;}
 bool isPressed(Button){return false;}
 bool wasTouchTapped(TouchPoint& p,int){if(!taps)return false;--taps;p={100,200};return true;}
 bool takeTouchHomeButtonPress(unsigned long&){return false;}
};
struct GlobalMenuActivity {
 enum class ModalResult {Dismissed,ShutdownRequested,Unavailable};
 static ModalResult runFirmwareModal(int,MappedInputManager&){assert(false);return ModalResult::Unavailable;}
};
struct Session{std::string launchPath;MappedInputManager input;int renderer=0;bool inputStarted=false,backExitsApp=true,exiting=false,presenting=false,pendingHomeSingle=false;unsigned long lastHomeEventMs=0;};
static Session active;
static Session* current(){return &active;}
static bool homeRequested=false;
'''
test=r'''
int main(){
 t5_app_input_t out{};
 assert(pollInput(&out,0,false));assert(!out.tapped && boundaries==1);
 for(unsigned i=0;i<20;++i){taps=1;assert(pollInput(&out,5,true));assert(out.tapped && out.touch_x==100);}
 assert(boundaries==1); // Never flush once per frame, which would lose real taps.
 active=Session{};taps=1;assert(pollInput(&out,0,false));assert(!out.tapped && boundaries==2);
 assert(ownerTicks==22); // Generic progress remains once per successful input poll.
 const unsigned before=ownerTicks;assert(!pollInput(nullptr,0,false));assert(ownerTicks==before);
 active.launchPath="next.elf";requestIdle=true;
 assert(pollInput(&out,0,false));assert(out.exit_requested && active.exiting && active.launchPath.empty());
 puts("native app first-input boundary drops prior-screen taps and preserves subsequent taps PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    cpp=Path(temp)/'test.cpp';binary=Path(temp)/'test'
    cpp.write_text(prefix+helper+poll+test)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                    '-I'+str(root/'lib/NativeApps/include'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=5)
