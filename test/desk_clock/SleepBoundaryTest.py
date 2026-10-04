"""Execute production sleep refusal and queued-transition code with bounded fakes."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def function(path,name):
 s=(ROOT/path).read_text();a=s.index(name);b=s.index('{',a);depth=1;i=b+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
def run(text):
 with tempfile.TemporaryDirectory() as t:
  p=Path(t)/'test.cpp';b=Path(t)/'test';p.write_text(text)
  subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'src'),str(p),'-o',str(b)],check=True)
  subprocess.run([str(b)],check=True,timeout=10,env=os.environ)
run(r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct Restart{};
#define LOG_ERR(...) ((void)0)
bool prep=true,releaseOk=true,commit=true,restore=true;
unsigned releaseCalls=0,restoreCalls=0,commitCalls=0,deinitCalls=0;
namespace Board { bool prepareForSleep(){return prep;} void deinitForSleep(){++deinitCalls;} }
bool halStorageCommitSleep(){++commitCalls;return commit;}
bool x4ReleaseDisplayForSleep(){++releaseCalls;return releaseOk;}
bool x4RestoreDisplayAfterSleep(){++restoreCalls;return restore;}
struct {void restart(){throw Restart{};}} ESP;
class HalDisplay {public:bool deepSleep();};
'''+function('lib/hal/HalDisplayX4.cpp','bool HalDisplay::deepSleep()')+r'''
int main(){
 HalDisplay display;assert(display.deepSleep() && deinitCalls==1);
 for(int failure=0;failure<3;++failure) for(bool reversible:{false,true}){
  prep=failure!=0;releaseOk=failure!=1;commit=failure!=2;restore=reversible;
  releaseCalls=restoreCalls=commitCalls=deinitCalls=0;
  bool restarted=false;
  try{assert(!display.deepSleep());}catch(const Restart&){restarted=true;}
  assert(restarted==!reversible && restoreCalls==1 && deinitCalls==0);
  assert(releaseCalls==(failure==0?0u:1u));
  assert(commitCalls==(failure==2?1u:0u));
 }
 std::puts("X4 production sleep barrier: prepare/release/commit refusal, checked restore and terminal recovery PASS");
}
''')
run(r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
using ActivityResult=int;
enum class DisplayPresentMode {Clean,Quality};
struct Renderer {void requestNextRefresh(DisplayPresentMode){}};
struct RenderLock {void unlock(){}};
struct MappedInputManager {
 struct TouchPoint{int x=0,y=0;};enum class Button{Back};
 bool getTouchSwipe(TouchPoint&,TouchPoint&,Renderer&){return false;}
 bool wasTouchHomeButtonPressed(){return false;}
 bool wasTouchTapped(TouchPoint&,Renderer&){return false;}
 void injectButtonTap(Button){} void clearInjectedButtonTap(){}
};
namespace StartupScreen{bool isLoading(){return false;}}
struct{bool doubleClickHomeMenu=false;}SETTINGS;
unsigned long millis(){return 0;}
constexpr unsigned long kDoubleClickWindowMs=400;
constexpr int eIncrement=1;
void xTaskNotify(void*,int,int){}
std::string transitionCalls;
uint32_t touchEpoch=0;
void nativeTouchBeginSurfaceTransition(bool requirePresentation){
 assert(requirePresentation);++touchEpoch;transitionCalls+='f';
}
uint32_t nativeTouchPresentationEpoch(){return touchEpoch;}
void nativeTouchCompleteSurfaceTransition(uint32_t epoch){
 assert(epoch==touchEpoch);transitionCalls+='c';
}
struct Activity {
 std::string name;ActivityResult result=0;
 std::function<void(ActivityResult)>resultHandler;
 std::function<void()>loopFn,enterFn,exitFn;
 bool supportsGlobalMenu(){return false;}bool onTouchSwipe(int,int,int,int){return false;}
 bool supportsTouchHomeButton(){return false;}bool onTouchHomeButton(){return false;}
 void onGoHome(){}bool showsHomeTouchButton(){return false;}bool isHomeTouchTap(int,int){return false;}
 bool resolveTouchButtonHint(int,int,MappedInputManager::Button&){return false;}
 bool onTouchTap(int,int){return false;}
 void loop(){if(loopFn)loopFn();}void onEnter(){if(enterFn)enterFn();}void onExit(){if(exitFn)exitFn();}
};
struct ActivityManager {
 enum class PendingAction{None,Pop,Replace,Push};
 std::unique_ptr<Activity>currentActivity,pendingActivity;
 std::vector<std::unique_ptr<Activity>>stackActivities;
 PendingAction pendingAction=PendingAction::None;
 DisplayPresentMode pendingReplaceRefreshMode=DisplayPresentMode::Clean;
 bool pendingHomeSingle=false,requestedUpdate=false;unsigned long lastHomeEventMs=0;
 void*renderTaskHandle=nullptr;Renderer renderer;MappedInputManager mappedInput;
 void openGlobalMenu(){}void requestUpdate(){requestedUpdate=true;}
 void exitActivity(const RenderLock&){if(currentActivity){currentActivity->onExit();currentActivity.reset();}}
 void goHome(){pendingAction=PendingAction::None;currentActivity=std::make_unique<Activity>();}
 void loop();
};
'''+function('src/activities/ActivityManager.cpp','void ActivityManager::loop()')+r'''
int main(){
 ActivityManager manager;unsigned loops=0,exits=0,enters=0;
 manager.currentActivity=std::make_unique<Activity>();manager.currentActivity->name="Settings";
 manager.currentActivity->loopFn=[&]{++loops;manager.pendingAction=ActivityManager::PendingAction::Pop;manager.pendingActivity.reset();};
 manager.currentActivity->exitFn=[&]{++exits;transitionCalls+='x';};
 manager.pendingActivity=std::make_unique<Activity>();manager.pendingActivity->name="Sleep";
 manager.pendingActivity->enterFn=[&]{++enters;transitionCalls+='e';};
 manager.pendingAction=ActivityManager::PendingAction::Replace;
 manager.loop();
 assert(loops==0 && exits==1 && enters==1 && manager.currentActivity->name=="Sleep");
 assert(transitionCalls=="xfec" && touchEpoch==1);
 manager.currentActivity->loopFn=[&]{++loops;};manager.loop();assert(loops==1);
 assert(transitionCalls=="xfec" && touchEpoch==1);
 std::puts("Production activity transition: pending Sleep cannot be replaced by outgoing finish/relaunch PASS");
}
''')
run(r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <sys/time.h>
#include "util/DeskClockTime.h"
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define BOARD_XTEINK_X4_PRO 1
#define SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP 0
constexpr int ESP_OK=0,ESP_FAIL=-1,INPUT_PULLUP=1,ESP_SLEEP_WAKEUP_ALL=0;
constexpr int ESP_PD_DOMAIN_RTC_PERIPH=1,ESP_PD_OPTION_ON=1,RTC_GPIO_MODE_INPUT_ONLY=1,ESP_EXT1_WAKEUP_ANY_LOW=0;
using esp_err_t=int;using gpio_num_t=int;
namespace BoardPins{constexpr unsigned PowerButton=3;}
struct Restart{};struct Slept{};
struct{uint32_t magic=123;}clockState;
bool power=true,timer=true,button=true;unsigned powered=0,padCalls=0,padFailure=0,armed=0;
uint64_t waitTime=0;bool slept=false;
struct{bool deepSleep(){++powered;return power;}}display;
struct{void restart(){throw Restart{};}}ESP;
void pinMode(unsigned,int){}
int fake_gettimeofday(timeval*out,void*){assert(powered==1);out->tv_sec=125;out->tv_usec=250000;return 0;}
#define gettimeofday fake_gettimeofday
void esp_sleep_disable_wakeup_source(int){assert(powered==1);}
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us){waitTime=us;return timer?ESP_OK:ESP_FAIL;}
esp_err_t pad(){return ++padCalls==padFailure?ESP_FAIL:ESP_OK;}
esp_err_t esp_sleep_pd_config(int,int){return pad();}
esp_err_t rtc_gpio_init(gpio_num_t){return pad();}
esp_err_t rtc_gpio_set_direction(gpio_num_t,int){return pad();}
esp_err_t rtc_gpio_pullup_en(gpio_num_t){return pad();}
esp_err_t rtc_gpio_pulldown_dis(gpio_num_t){return pad();}
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask,int){assert(mask==(1ULL<<3));++armed;return button?ESP_OK:ESP_FAIL;}
void esp_deep_sleep_start(){slept=true;throw Slept{};}
'''+function('src/DeskClockSleep.cpp','void sleepUntilNextMinute()')+r'''
void reset(){power=timer=button=true;powered=padCalls=padFailure=armed=0;waitTime=0;slept=false;clockState.magic=123;}
int main(){
 reset();try{sleepUntilNextMinute();assert(false);}catch(const Slept&){}
 assert(slept && armed==1 && waitTime==54750000u && clockState.magic==123);
 reset();power=false;sleepUntilNextMinute();assert(!slept && !armed && !waitTime && !clockState.magic);
 for(unsigned failure=1;failure<=5;++failure){reset();padFailure=failure;
  try{sleepUntilNextMinute();assert(false);}catch(const Restart&){}
  assert(!slept && !clockState.magic);
 }
 reset();timer=false;try{sleepUntilNextMinute();assert(false);}catch(const Restart&){}assert(!slept && !clockState.magic);
 reset();button=false;try{sleepUntilNextMinute();assert(false);}catch(const Restart&){}assert(!slept && !clockState.magic);
 std::puts("Production minute sleep: deadline after teardown, timer/button wake and checked RTC pad failures PASS");
}
''')
run(r'''
#include <cassert>
#include <cstdio>
#include <string>
#define LOG_ERR(...) ((void)0)
std::string calls; char fail=0;
bool nativeTouchSuspend(){calls+='t';return fail!='t';}
bool nativeBatterySuspend(){calls+='b';return fail!='b';}
bool nativeRtcSuspend(){calls+='r';return fail!='r';}
bool nativeNavigationSuspend(){calls+='n';return fail!='n';}
bool nativeTouchResume(){calls+='T';return true;}
bool nativeBatteryResume(){calls+='B';return true;}
bool nativeRtcResume(){calls+='R';return true;}
void nativeNavigationResume(){calls+='N';}
'''+function('src/main.cpp','bool suspendInputProvidersForSleep()')+'\n'+
function('src/main.cpp','void resumeInputProvidersAfterSleep()')+r'''
int main(){
 assert(suspendInputProvidersForSleep() && calls=="tbrn");
 calls.clear();resumeInputProvidersAfterSleep();assert(calls=="NRBT");
 struct Failure{char who;const char*expected;};
 for(auto f:{Failure{'t',"tT"},Failure{'b',"tbBT"},Failure{'r',"tbrRBT"},Failure{'n',"tbrnNRBT"}}){
  calls.clear();fail=f.who;assert(!suspendInputProvidersForSleep() && calls==f.expected);
  calls.clear();fail=0;assert(suspendInputProvidersForSleep() && calls=="tbrn");
 }
 puts("Production sleep consumers: RTC precedes graph drain, every rollback and retry PASS");
}
''')
