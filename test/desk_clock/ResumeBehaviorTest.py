"""Exercise the production X4 clock-resume dispatcher with bounded fake edges."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'src/DeskClockSleep.cpp').read_text()
def function(name):
    begin = SOURCE.index(name)
    at = SOURCE.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (SOURCE[at] == '{') - (SOURCE[at] == '}')
        at += 1
    return SOURCE[begin:at]

# IDF4.4.7 reloads .rtc.data on non-deep-sleep resets; .rtc_noinit is excluded.
noinit = 'RTC_NOINIT_ATTR uint32_t clockUiWakeMagic;' in SOURCE
prefix = r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#define BOARD_XTEINK_X4_PRO 1
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
using esp_sleep_wakeup_cause_t=int;using gpio_num_t=int;
constexpr int ESP_OK=0,INPUT_PULLUP=1,ESP_SLEEP_WAKEUP_UNDEFINED=0;
constexpr int ESP_SLEEP_WAKEUP_TIMER=4,ESP_SLEEP_WAKEUP_EXT1=3;
constexpr int ESP_RST_SW=3,ESP_RST_POWERON=1,ESP_RST_DEEPSLEEP=8;
namespace BoardPins { constexpr int PowerButton=3; }
namespace DeskClockSleep { bool resumeAfterTimerWake();bool consumeUserWake(); }
int wake=0,resetCause=ESP_RST_POWERON,padCalls=0,padFailure=0;
unsigned loads=0,configured=0,frames=0,painted=0,pinModes=0;
bool valid=false,provider=true,buffer=true,paintOk=true;
int esp_sleep_get_wakeup_cause(){return wake;}
int esp_reset_reason(){return resetCause;}
int pad(int pin){assert(pin==3);return ++padCalls==padFailure ? -1 : ESP_OK;}
int rtc_gpio_hold_dis(int pin){return pad(pin);}
int rtc_gpio_deinit(int pin){return pad(pin);}
void pinMode(int pin,int mode){assert(pin==3 && mode==INPUT_PULLUP);++pinModes;}
bool x4BeginClockDisplay(){++loads;return provider;}
struct { bool isSystemTimeValid(){return valid;}
    void configure(const char*,bool,uint8_t,uint32_t){++configured;}
}halClock;
enum class Language{};struct {void setLanguage(Language){}}I18N;
struct {void begin(bool clear){assert(!clear);++frames;}
    void*getFrameBuffer(){return buffer?this:nullptr;}
    void setFlipOutput(bool){}
}display;
struct GfxRenderer{void begin(){}void insertFont(int,int){}}renderer;
constexpr int UI_12_FONT_ID=1,SMALL_FONT_ID=2,ui12FontFamily=0,smallFontFamily=0;
struct Slept{};
'''
retention = SOURCE[SOURCE.index('constexpr uint32_t kClockMagic'):SOURCE.index('void renderClockFrame')]
body = prefix + retention + r'''
void paintAndSleep(GfxRenderer&,bool timerWake){assert(timerWake);++painted;if(paintOk)throw Slept{};clockState.magic=0;}
''' + function('bool DeskClockSleep::resumeAfterTimerWake()') + '\n' + function('bool DeskClockSleep::consumeUserWake()') + r'''
void defaults(){
    wake=0;resetCause=ESP_RST_POWERON;padCalls=padFailure=0;
    loads=configured=frames=painted=pinModes=0;valid=false;provider=buffer=paintOk=true;
    clockState={};clockUiWakeMagic=0;userWakePending=false;
}
void retained(int cause){wake=cause;resetCause=ESP_RST_DEEPSLEEP;clockState.magic=kClockMagic;}
int main(){
    for(bool timeValid:{false,true}){
        defaults();valid=timeValid;assert(!DeskClockSleep::resumeAfterTimerWake());
        assert(!DeskClockSleep::consumeUserWake() && !loads && !clockState.magic && pinModes==1);
        defaults();valid=timeValid;retained(ESP_SLEEP_WAKEUP_EXT1);
        assert(!DeskClockSleep::resumeAfterTimerWake());
        assert(DeskClockSleep::consumeUserWake() && !DeskClockSleep::consumeUserWake());
        assert(!loads && padCalls==2 && !clockState.magic);
    }
    // An unset clock doesn't read/load SD display packages before normal boot.
    defaults();retained(ESP_SLEEP_WAKEUP_TIMER);
    assert(!DeskClockSleep::resumeAfterTimerWake() && !loads && !clockState.magic);
    defaults();valid=true;retained(ESP_SLEEP_WAKEUP_TIMER);
    try{DeskClockSleep::resumeAfterTimerWake();assert(false);}catch(const Slept&){}
    assert(loads==1 && configured==1 && frames==1 && painted==1 && clockState.magic==kClockMagic);
    for(int failure:{1,2,3}){
        defaults();valid=true;retained(ESP_SLEEP_WAKEUP_TIMER);
        provider=failure!=1;buffer=failure!=2;paintOk=failure!=3;
        assert(!DeskClockSleep::resumeAfterTimerWake() && !clockState.magic);
        assert(!DeskClockSleep::consumeUserWake());
    }
    for(int failure:{1,2}){
        defaults();valid=true;retained(ESP_SLEEP_WAKEUP_EXT1);padFailure=failure;
        assert(!DeskClockSleep::resumeAfterTimerWake() && !clockState.magic && !loads);
        assert(padCalls==failure);
    }
    // A press during repaint asks for a software reset. The bootloader reloads
    // DATA/BSS, but the noinit marker must survive until this one consumption.
    defaults();clockUiWakeMagic=kClockUiWakeMagic;resetCause=ESP_RST_SW;
#if !UI_FLAG_NOINIT
    clockUiWakeMagic=0;
#endif
    assert(!DeskClockSleep::resumeAfterTimerWake() && DeskClockSleep::consumeUserWake());
    assert(!clockUiWakeMagic && !DeskClockSleep::consumeUserWake());
    // A stale/garbage marker on a cold reset is rejected and scrubbed.
    defaults();clockUiWakeMagic=kClockUiWakeMagic;
    assert(!DeskClockSleep::resumeAfterTimerWake() && !DeskClockSleep::consumeUserWake() && !clockUiWakeMagic);
    defaults();clockUiWakeMagic=0xdeadbeef;resetCause=ESP_RST_SW;
    assert(!DeskClockSleep::resumeAfterTimerWake() && !DeskClockSleep::consumeUserWake() && !clockUiWakeMagic);
    puts("Production X4 resume: cold/button/timer, valid/unset time, failure/recovery, software-reset wake PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source=Path(tmp)/'test.cpp'; binary=Path(tmp)/'test';source.write_text(body)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-DUI_FLAG_NOINIT='+str(int(noinit)),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=10,env=os.environ)
