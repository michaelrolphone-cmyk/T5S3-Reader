#!/usr/bin/env python3
"""Actual touch consumer + renderer + native launch/poll/error routes under stalls.

Only the physical provider, RTOS, filesystem/ELF and display I/O are fixtures.
The production function bodies are compiled, rather than mirroring their guards.
"""
from pathlib import Path
import ast
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

# Reuse the scripted physical provider from the capture regression, without
# executing that runner or substituting any production consumer logic.
fixture_ast = ast.parse((ROOT/'test/hal/touch_capture_behavior_test.py').read_text())
strings = {n.targets[0].id: ast.literal_eval(n.value) for n in fixture_ast.body
           if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)
           and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str)}
touch = (ROOT/'src/native/NativeTouchInput.cpp').read_text()
consumer = (touch[touch.index('namespace {'):touch.index('bool workerShouldRun()')]
            + '\n}\n' + touch[touch.index('void nativeTouchDiscardGestures('):])
consumer += '\nnamespace { bool activate(){return true;} }\n' + method(touch, 'void nativeTouchTick(')
provider = strings['fixture'].split('int main(')[0]
host = (ROOT/'src/native/NativeAppHost.cpp').read_text()
gfx = (ROOT/'lib/GfxRenderer/GfxRenderer.cpp').read_text()
prefix = r'''
#include <algorithm>
#include <functional>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <T5AppApi.h>
#include <DisplaySurface.h>
static unsigned ownerTicks, launches, updates;
static bool requestIdle, physicalBack, physicalPower, sleepRequested;
static std::function<void()> launchHook, storageHook, presentHook, updateHook;
static unsigned lastRenderFlags;
static void delay(unsigned ms){nowMs+=ms;}
static void esp_task_wdt_reset(){}
static void nativeProviderOwnerTick(){++ownerTicks;}
static void nativeVideoServiceTouchPresentation(){}
bool nativeTouchHadActivity(){return touchActive;}
static bool serviceIdleSleep(bool){return requestIdle;}
static bool idleSleepRequested(){return sleepRequested;}
static void nativeNavigationBoundary(){}
static void nativeNavigationRetry(){}
struct Nav{unsigned buttons=0;};
static Nav nativeNavigationFrame(){return {};}
static bool nativeHardwareTakeoverDisplayActive(){return false;}
struct {bool doubleClickHomeMenu=false;} SETTINGS;
constexpr unsigned long kNativeHomeDoubleClickWindowMs=400;
struct Surface final : DisplaySurface {
 bool ready=true,success=true,finished=false;
 uint8_t pixels[8]{};
 bool isReady()const override{return ready;}
 bool lastPresentSucceeded()const override{return finished;}
 DisplaySurfaceInfo getSurfaceInfo()const override{return {};}
 uint8_t*getFrameBuffer()const override{return const_cast<uint8_t*>(pixels);}
 void clearScreen(uint8_t)const override{}
 void drawImage(const uint8_t*,uint16_t,uint16_t,uint16_t,uint16_t,bool)const override{}
 void drawImageTransparent(const uint8_t*,uint16_t,uint16_t,uint16_t,uint16_t,bool)const override{}
 void displayBuffer(DisplayPresentMode,bool=false)override{finished=false;if(presentHook)presentHook();finished=ready&&success;}
 void displayGrayBuffer(DisplayPresentMode m)override{displayBuffer(m);}
 void requestNextRefresh(DisplayPresentMode)override{}
 void requestNextDisplayEffect(DisplayEffect)override{}
 void copyGrayscaleLsbBuffers(const uint8_t*)override{}
 void copyGrayscaleMsbBuffers(const uint8_t*)override{}
 bool captureGrayscaleBaseBuffer(const uint8_t*)override{return true;}
 bool grayscaleBuffersReady()const override{return true;}
 void cleanupGrayscaleBuffers(const uint8_t*)override{}
};
struct GfxRenderer {
 enum Mode{BW};
 Surface backing;
 DisplaySurface&display=backing;
 bool initialized=true;
 uint8_t*frameBuffer=backing.pixels;
 unsigned long start_ms=0;
 int getOrientation(){return 0;} Mode getRenderMode(){return BW;}
 void setRenderMode(Mode){} void setOrientation(int){}
 void clearScreen(){} void drawText(int,int,int,const char*){}
 void requestNextRefresh(DisplayPresentMode){}
 void displayBuffer(DisplayPresentMode=DisplayPresentMode::Quality,bool=true)const;
 void displayGrayBuffer(DisplayPresentMode)const;
};
struct MappedInputManager {
 enum class Button{Back,Confirm,Left,Right,Up,Down,Power};
 using TouchPoint=NativeTouchPoint;
 void update(){++updates;serviceProvider();nativeTouchTick();if(updateHook)updateHook();}
 void clearInjectedButtonTap(){}
 bool wasAnyPressed(){return physicalBack||physicalPower;}
 bool wasAnyReleased(){return false;}
 bool isPressed(Button b){return (b==Button::Back&&physicalBack)||(b==Button::Power&&physicalPower);}
 bool wasTouchTapped(TouchPoint&p,GfxRenderer&){return nativeTouchGetTap(p);}
 bool wasTouchHomeButtonPressed(){return nativeTouchTakeHomePress();}
 bool takeTouchHomeButtonPress(unsigned long&t){return nativeTouchTakeHomePress(t);}
};
struct HalFile{bool isOpen(){return false;}void close(){}};
struct CatalogAsset{};
struct ToneRectCommand{};
struct Manifest{char display_name[32]="Test",icon[32]="",file_name[32]="app.elf",min_firmware_version[32]="0";bool compatible=true;};
'''
# Use the production Session layout; manifest contents don't matter for input.
# T5AppApi supplies the actual manifest ABI consumed by runNativeApp.
session = host[host.index('struct Session {'):host.index('int32_t width()')]
stubs = r'''
static TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(1);}
'''
# xTaskGetCurrentTaskHandle is declared before the actual current() body.
prefix += stubs
rest = r'''
struct RenderLock{};
namespace HalPowerManager{struct Lock{};}
struct GlobalMenuActivity{enum class ModalResult{Dismissed,ShutdownRequested,Unavailable};
 static ModalResult runFirmwareModal(GfxRenderer&,MappedInputManager&){return ModalResult::Dismissed;}};
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_ERR_INVALID_ARG=2,ESP_ERR_NOT_SUPPORTED=3;
constexpr const char*CROSSPOINT_COMPAT_VERSION="1.3.102";
constexpr int UI_12_FONT_ID=12;
static bool validManifest=true, storageReady=true, resolveOkay=true;
static bool risc_runtime_retention_required(){return false;}
static bool native_app_loader_retained(){return false;}
namespace NativeReaderEntry{static bool mapped(){return false;}static bool pending(){return false;}}
namespace RuntimeDevices{struct AppCapabilityRequirements{unsigned count=0;};}
#ifdef READER_TEST_REAL_CONTEXT
#else
namespace RuntimeResources{struct ExecutionContext{bool begin(){return true;}void end(){}};}
#endif
static bool readAppManifest(const char*,t5_app_manifest_t&out,void* =nullptr,bool =false,RuntimeDevices::AppCapabilityRequirements* =nullptr){if(storageHook)storageHook();out={};strcpy(out.file_name,"app.elf");strcpy(out.display_name,"Test");out.compatible=true;return validManifest;}
static bool t5_safe_elf_name(const char*){return true;}
struct StorageMock{bool ready(){return storageReady;}bool exists(const char*p){return std::string(p).find(".bak")==std::string::npos;}}Storage;
namespace RuntimePackages{
 struct Identity{char artifact[64]="app.elf";};
 static bool recoverAppPair(const char*){return true;}
 static bool recoverAppInventory(){if(storageHook)storageHook();return true;}
 static bool inspectInstalledAppPair(const char*,const char*,const char*){return true;}
 struct UseGate{bool pin(const char*){return true;}bool unpin(const char*){return true;}}gate;
 static UseGate&systemPackageUseGate(){return gate;}
 static bool beginManagedAppAdmission(const Identity&,const char*){return true;}
 static bool beginLooseAppAdmission(const char*,bool(*)(const char*)){return true;}
 static void endManagedAppAdmission(){}
}
static bool validateLooseAdmissionSidecar(const char*){return true;}
static bool verifiedManagedApp(const char*,RuntimePackages::Identity&){return true;}
static bool resolveInstalledAppPath(const char*,std::string&out){out="/sd/Apps/app.elf";return resolveOkay;}
static void nativeNetworkBegin(){}static void nativeNetworkEnd(){}
static void nativeSettingsBegin(GfxRenderer&,MappedInputManager&){}static void nativeSettingsEnd(){}
static void nativeSystemUiBegin(){}static void nativeStreamsBegin(){}static void nativeStreamsEnd(){}
static bool nativeStreamsBindPackageResources(const RuntimePackages::Identity&){return true;}
static const char*native_hardware_compat_last_error(){return nullptr;}
namespace GpsDriverRuntime{static void stop(){}}
enum class NativeSystemUiNavigation{Home,Keyboard,None};
static NativeSystemUiNavigation nativeSystemUiTakeNavigation(){return NativeSystemUiNavigation::None;}
static bool nativeSettingsDispatchPendingAction(GfxRenderer&,MappedInputManager&,const char*){return false;}
namespace StartupScreen{static void app(GfxRenderer&r,const char*,const char*){r.displayBuffer(DisplayPresentMode::Quality,false);}}
static int launch_elf_reader_entry(const char*){return ESP_ERR_INVALID_STATE;}
static int launch_elf_app(const char*){++launches;if(launchHook)launchHook();return ESP_OK;}
'''
bodies = '\n'.join(method(gfx, sig) for sig in ('void GfxRenderer::displayBuffer(', 'void GfxRenderer::displayGrayBuffer('))
bodies += '\n' + method(host, 'bool pollInput(')
bodies += '\n' + method(host, '[[noreturn]] static void retainNativeAppSession(')
bodies += '\n' + method(host, 'static esp_err_t runNativeAppImpl(')
bodies += '\n' + method(host, 'esp_err_t runNativeApp(')
bodies += '\n' + method(host, 'bool runNativeSpringboard(')
tests = r'''
static void tap(){queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();nowMs+=20;queue(RISC_TOUCH_EVENT_UP);serviceProvider();}
static void reset(){
 clearTransient();events.clear();state={};serial=0;focusRequested=focusApplied=0;
 surfaceTransitionPending=coordinatesSuppressed=false;api=&provider;subscription=1;nowMs=1000;
 physicalBack=physicalPower=requestIdle=sleepRequested=false;validManifest=storageReady=resolveOkay=true;
 storageHook=launchHook=presentHook=updateHook={};session=nullptr;homeRequested=false;
}
int main(){
 GfxRenderer r;MappedInputManager input;t5_app_input_t out{};
 reset();
 // Actual launch's preview and admission can block for 25 seconds. Polling
 // before the app draws does not turn its loading frame into a touch target.
 bool blocked=false;storageHook=[&]{if(blocked)return;blocked=true;tap();nowMs+=25000;tap();};
 launchHook=[&]{
   assert(pollInput(&out,0,false)&&!out.tapped);
   tap();assert(pollInput(&out,0,false)&&!out.tapped);
   presentHook=[&]{tap();nowMs+=20000;tap();};
   r.displayBuffer(DisplayPresentMode::Quality);presentHook={};
   assert(pollInput(&out,0,false)&&!out.tapped);
   for(unsigned n=0;n<20;++n){tap();assert(pollInput(&out,0,false)&&out.tapped);}
   // Already-running ELF stalls: no stale replay, yet new input still works.
   const auto stallsBefore=nativeTouchDiagnostics().consumerStalls;
   tap();nowMs+=30000;tap();assert(pollInput(&out,0,false)&&!out.tapped);
   assert(nativeTouchDiagnostics().consumerStalls==stallsBefore+1);
   assert(pollInput(&out,0,false)&&!out.tapped);
   assert(nativeTouchDiagnostics().consumerStalls==stallsBefore+1);
   serviceProvider();tap();assert(pollInput(&out,0,false)&&out.tapped);
   // Continuous input service preserves a real long hold, even when apps only
   // request contact/hold late; absence of a getter is not a blocked consumer.
   queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();
   for(unsigned n=0;n<30;++n){nowMs+=1000;assert(pollInput(&out,0,false)&&!out.tapped);}
   NativeTouchPoint heldPoint{};unsigned long heldMs=0;
   assert(nativeTouchGetHold(heldPoint,heldMs)&&heldMs==30000);
   queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(pollInput(&out,0,false)&&out.tapped);
   // A contact across a genuinely stalled UI is neutral-gated until lift.
   queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();nowMs+=25000;
   assert(pollInput(&out,0,false)&&!out.tapped);serviceProvider();
   assert(!nativeTouchGetContact(heldPoint));
   queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(pollInput(&out,0,false)&&!out.tapped);
   tap();assert(pollInput(&out,0,false)&&out.tapped);
 };
 assert(runNativeApp("/sd/Apps/app.elf",r,input)==ESP_OK);
 assert(surfaceTransitionPending); // Parent must display its own destination.
 auto epoch=nativeTouchPresentationEpoch();r.displayBuffer();nativeTouchCompleteSurfaceTransition(epoch);serviceProvider();
 NativeTouchPoint fresh{};tap();assert(nativeTouchGetTap(fresh));
 // First visible frame starts the shared consumer clock even before the
 // first read: an app that blocks immediately cannot admit a recent tail tap.
 reset();launchHook=[&]{
   r.displayBuffer();serviceProvider();
   for(unsigned n=0;n<30;++n){nowMs+=1000;nativeTouchTick();} // background service is not UI dispatch
   tap();
   assert(pollInput(&out,0,false)&&!out.tapped);serviceProvider();
   tap();assert(pollInput(&out,0,false)&&out.tapped);
 };
 assert(runNativeApp("/sd/Apps/app.elf",r,input)==ESP_OK);
 // First-frame failure, skipped presentation, intermediate loading frame and
 // unsupported gray all stay fenced. A valid retry restores fresh input.
 reset();launchHook=[&]{
   r.backing.success=false;r.displayBuffer();assert(surfaceTransitionPending);
   r.backing.success=true;r.backing.ready=false;r.displayBuffer();assert(surfaceTransitionPending);
   r.backing.ready=true;r.displayBuffer(DisplayPresentMode::Quality,false);assert(surfaceTransitionPending);
   r.backing.success=false;r.displayGrayBuffer(DisplayPresentMode::Quality);assert(surfaceTransitionPending);
   r.backing.success=true;r.displayBuffer();assert(!surfaceTransitionPending);
   assert(pollInput(&out,0,false)&&!out.tapped);tap();assert(pollInput(&out,0,false)&&out.tapped);
   physicalBack=true;assert(pollInput(&out,0,false)&&out.exit_requested);physicalBack=false;
 };
 assert(runNativeApp("/sd/Apps/app.elf",r,input)==ESP_OK);
 // Early manifest failure returns with the deferred loading boundary intact.
 reset();unsigned manifests=0;storageHook=[&]{if(++manifests>1){tap();nowMs+=25000;tap();validManifest=false;}};
 assert(runNativeApp("/sd/Apps/app.elf",r,input)==ESP_ERR_NOT_SUPPORTED);
 assert(surfaceTransitionPending);NativeTouchPoint point{};assert(!nativeTouchGetTap(point));
 // Actual Springboard error loop: loading/recovery taps cannot auto-dismiss;
 // fresh error-page input dismisses, and a second tap cannot spill into Home.
 reset();resolveOkay=false;storageHook=[&]{tap();nowMs+=25000;tap();};unsigned errorPolls=0;
 updateHook=[&]{if(++errorPolls==2){tap();tap();}assert(errorPolls<5);};
 assert(!runNativeSpringboard(r,input,false)&&errorPolls==2);
 assert(surfaceTransitionPending&&!nativeTouchGetTap(point));
 updateHook={};storageHook={};resolveOkay=true;launchHook=[&]{r.displayBuffer();assert(pollInput(&out,0,false)&&!out.tapped);tap();assert(pollInput(&out,0,false)&&out.tapped);};
 assert(!runNativeSpringboard(r,input,false)); // legitimate retry returns normally
 // Physical escape is available even while no interactive frame has appeared.
 reset();launchHook=[&]{physicalPower=true;assert(pollInput(&out,0,false)&&out.exit_requested);physicalPower=false;};
 assert(runNativeApp("/sd/Apps/app.elf",r,input)==ESP_OK&&homeRequested);
 puts("Actual native launch/poll/error/retry + touch consumer: 20–30s stalls, first-frame failure, fresh input and Back/Power PASS");
}
'''
if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as d:
        cpp=Path(d)/'test.cpp';binary=Path(d)/'test'
        cpp.write_text(strings['prefix']+consumer+provider+prefix+session+rest+bodies+tests)
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-missing-field-initializers',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            '-I'+str(ROOT/'src/native'),'-I'+str(ROOT/'sdk/driver'),'-I'+str(ROOT/'lib/DisplaySurface'),
            '-I'+str(ROOT/'lib/NativeApps/include'),str(cpp),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'},check=True,timeout=15)
