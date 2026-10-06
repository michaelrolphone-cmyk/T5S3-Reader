#!/usr/bin/env python3
"""Execute actual ActivityManager transitions/render dispatch with touch capture."""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('native_focus',ROOT/'test/hal/native_app_focus_test.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
manager=(ROOT/'src/activities/ActivityManager.cpp').read_text()
prefix=r'''
#define BOARD_XTEINK_X4_PRO 1
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffu
#define taskENTER_CRITICAL(x) ((void)(x))
#define taskEXIT_CRITICAL(x) ((void)(x))
constexpr int eIncrement=1;
struct RenderDone{};
static unsigned renderWaits;
static unsigned ulTaskNotifyTake(int,unsigned){if(renderWaits++)throw RenderDone{};return 1;}
static void xTaskNotify(TaskHandle_t,int,int){}
namespace StartupScreen{static bool isLoading(){return false;}}
struct ActivityResult{};
struct Activity {
 std::string name="Screen";
 ActivityResult result;
 std::function<void(const ActivityResult&)>resultHandler;
 std::function<void()>enter,exit,paint,handleTap,handleSwipe,step;
 unsigned taps=0,swipes=0,loops=0;
 void onEnter(){if(enter)enter();}void onExit(){if(exit)exit();}
 void render(RenderLock&&){if(paint)paint();}
 bool supportsGlobalMenu(){return false;}bool supportsTouchHomeButton(){return true;}
 bool onTouchSwipe(int,int,int,int){++swipes;if(handleSwipe)handleSwipe();return false;}
 bool onTouchHomeButton(){return false;}void onGoHome(){}
 bool showsHomeTouchButton(){return false;}bool isHomeTouchTap(int,int){return false;}
 bool resolveTouchButtonHint(int,int,MappedInputManager::Button&){return false;}
 bool onTouchTap(int,int){++taps;if(handleTap)handleTap();return true;}
 void loop(){++loops;if(step)step();}
};
struct ActivityManager {
 GfxRenderer&renderer;MappedInputManager&mappedInput;
 std::unique_ptr<Activity>currentActivity,pendingActivity;
 std::vector<std::unique_ptr<Activity>>stackActivities;
 enum class PendingAction{None,Pop,Push,Replace};PendingAction pendingAction=PendingAction::None;
 DisplayPresentMode pendingReplaceRefreshMode=DisplayPresentMode::Clean;
 TaskHandle_t renderTaskHandle=nullptr,waitingTaskHandle=nullptr;
 uint64_t activityGeneration=0,deferredGeneration=0;
 Activity* deferredActivity=nullptr;
 int waitingTaskMux=0;bool requestedUpdate=false,pendingHomeSingle=false;
 unsigned long lastHomeEventMs=0;static constexpr unsigned long kDoubleClickWindowMs=400;
 ActivityManager(GfxRenderer&r,MappedInputManager&i):renderer(r),mappedInput(i){}
 void renderTaskLoop();void loop();void finishLoop();void exitActivity(const RenderLock&);
 void replaceActivity(std::unique_ptr<Activity>&&);
 void replaceActivity(std::unique_ptr<Activity>&&,DisplayPresentMode);
 void pushActivity(std::unique_ptr<Activity>&&);void popActivity();
 void requestUpdate(){requestedUpdate=true;}void goHome(){replaceActivity(std::make_unique<Activity>());}
 void openGlobalMenu(){}
};
'''
methods='\n'.join(f.method(manager,sig) for sig in (
 'void ActivityManager::renderTaskLoop(', 'void ActivityManager::loop(', 'void ActivityManager::finishLoop(',
 'void ActivityManager::exitActivity(',
 'void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {',
 'void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity,',
 'void ActivityManager::pushActivity(', 'void ActivityManager::popActivity('))
tests=r'''
static void tap(){queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();nowMs+=20;queue(RISC_TOUCH_EVENT_UP);serviceProvider();}
static void renderOnce(ActivityManager&m){renderWaits=0;try{m.renderTaskLoop();}catch(RenderDone&){}serviceProvider();}
int main(){
 api=&provider;subscription=1;nowMs=1000;GfxRenderer r;MappedInputManager input;ActivityManager m(r,input);
 auto screen=std::make_unique<Activity>();screen->paint=[&]{r.displayBuffer();};
 m.replaceActivity(std::move(screen));renderOnce(m);
 tap();m.loop();assert(m.currentActivity->taps==1);
 // Actual replace route: a 25s onEnter delay captures input independently.
 auto next=std::make_unique<Activity>();next->enter=[&]{tap();nowMs+=25000;tap();};
 next->paint=[&]{tap();r.displayBuffer();};
 m.replaceActivity(std::move(next));m.loop();assert(surfaceTransitionPending);
 m.loop();assert(m.currentActivity->taps==0);renderOnce(m);
 assert(!surfaceTransitionPending);m.loop();assert(m.currentActivity->taps==0);
 tap();m.loop();assert(m.currentActivity->taps==1);
 // BMP-style activities present synchronously in onEnter with no render task.
 auto sync=std::make_unique<Activity>();sync->enter=[&]{tap();nowMs+=20000;tap();r.displayBuffer();};
 sync->paint=[&]{r.displayBuffer();};
 m.replaceActivity(std::move(sync));m.loop();serviceProvider();assert(!surfaceTransitionPending);
 tap();m.loop();assert(m.currentActivity->taps==1);
 // Push/pop and result handler delays cannot transfer child taps to parent.
 auto child=std::make_unique<Activity>();child->paint=[&]{r.displayBuffer();};
 m.currentActivity->resultHandler=[&](const ActivityResult&){tap();nowMs+=30000;tap();};
 m.pushActivity(std::move(child));m.loop();renderOnce(m);tap();m.popActivity();m.loop();
 assert(surfaceTransitionPending);m.loop();assert(m.currentActivity->taps==1);
 renderOnce(m);tap();m.loop();assert(m.currentActivity->taps==2);
 // Loading popup, failed/aborted final render and a later retry are distinct.
 m.currentActivity->exit=[&]{r.displayBuffer();}; // Outgoing BMP cleanup is not next-screen readiness.
 auto retry=std::make_unique<Activity>();bool fail=true;
 retry->paint=[&]{r.displayBuffer(DisplayPresentMode::Quality,false);tap();if(fail)return;r.displayBuffer();};
 m.replaceActivity(std::move(retry));m.loop();renderOnce(m);
 assert(surfaceTransitionPending);m.loop();assert(m.currentActivity->taps==0);
 fail=false;renderOnce(m);tap();m.loop();assert(m.currentActivity->taps==1);
 // A noninteractive bookmark toast after a valid final frame doesn't undo it.
 auto toast=std::make_unique<Activity>();toast->paint=[&]{r.displayBuffer();r.displayBuffer(DisplayPresentMode::Quality,false);};
 m.replaceActivity(std::move(toast));m.loop();renderOnce(m);assert(!surfaceTransitionPending);

 // Swipe changes focus but returns false: Manager must not dispatch a second
 // queued tap or outgoing loop before applying the new destination.
 unsigned stray=0;m.currentActivity->handleTap=[&]{++stray;};
 m.currentActivity->handleSwipe=[&]{auto a=std::make_unique<Activity>();a->paint=[&]{r.displayBuffer();};m.pushActivity(std::move(a));};
 queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_MOVE,180);queue(RISC_TOUCH_EVENT_UP,180);serviceProvider();tap();
 m.loop();assert(stray==0&&surfaceTransitionPending);renderOnce(m);m.loop();assert(m.currentActivity->taps==0);
 puts("Actual ActivityManager: replace/push/pop, 25–30s preparation/result stalls, failed popup/render retry and single-surface dispatch PASS");
}
'''
# Fixture wrappers keep real NativeTouch getters and production Manager bodies.
base=f.strings['prefix']+f.consumer+f.provider+f.prefix+f.session+f.rest+f.bodies
base=base.replace('struct RenderLock{};','struct RenderLock{void unlock(){}};')
base=base.replace(' void clearInjectedButtonTap(){}',' void clearInjectedButtonTap(){}\n void injectButtonTap(Button){}\n bool getTouchSwipe(TouchPoint&a,TouchPoint&b,GfxRenderer&){return nativeTouchGetSwipe(a,b);}')
with tempfile.TemporaryDirectory() as d:
 cpp=Path(d)/'test.cpp';binary=Path(d)/'test';cpp.write_text(base+prefix+methods+tests)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
  '-Wno-unused-parameter','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-omit-frame-pointer',
  '-I'+str(ROOT/'src/native'),'-I'+str(ROOT/'sdk/driver'),'-I'+str(ROOT/'lib/DisplaySurface'),
  '-I'+str(ROOT/'lib/NativeApps/include'),str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'},check=True,timeout=15)
