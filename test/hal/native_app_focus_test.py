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
#include <T5AppApi.h>
static unsigned boundaries=0, taps=1, ownerTicks=0;
void nativeProviderOwnerTick(){++ownerTicks;}
void nativeTouchDiscardGestures(){++boundaries;taps=0;}
void esp_task_wdt_reset(){}
void delay(unsigned){}
struct MappedInputManager {
 enum class Button{Back,Confirm,Left,Right,Up,Down,Power};
 struct TouchPoint{int16_t x=0,y=0;};
 void update(){}
 bool isPressed(Button){return false;}
 bool wasTouchTapped(TouchPoint& p,int){if(!taps)return false;--taps;p={100,200};return true;}
 bool wasTouchHomeButtonPressed(){return false;}
};
struct Session{MappedInputManager input;int renderer=0;bool inputStarted=false,backExitsApp=true,exiting=false;};
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
 puts("native app first-input boundary drops prior-screen taps and preserves subsequent taps PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    cpp=Path(temp)/'test.cpp';binary=Path(temp)/'test'
    cpp.write_text(prefix+helper+poll+test)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                    '-I'+str(root/'lib/NativeApps/include'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=5)
