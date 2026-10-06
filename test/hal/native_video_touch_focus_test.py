#!/usr/bin/env python3
"""Real NativeVideoBridge/NativeAppHost/touch pipeline with staged scan completion."""
from pathlib import Path
import importlib.util
import os
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('native_focus',ROOT/'test/hal/native_app_focus_test.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
bridge=(ROOT/'src/native/NativeVideoBridge.cpp').read_text()
bridge=re.sub(r'^#include .*\n','',bridge,flags=re.M)
bridge=bridge[bridge.index('extern "C" bool'):bridge.index('\n#else')]
video=r'''
#define READER_TEST_REAL_CONTEXT
#include <T5VideoApi.h>
#include <T5DisplayProviderV1.h>
#include "runtime/resources/ExecutionContext.h"
static RuntimeResources::ExecutionContext appContext;
static bool ownerTask=true,borrowed=true,engineRunning=false,drivePending=false,submitOkay=true,startOkay=true,stopOkay=true;
static uint32_t scanCounter,submissions;
extern "C" bool native_hardware_display_is_borrowed(){return borrowed;}
static uint32_t nativeProviderStreamConsumer(){
 auto*ctx=RuntimeResources::ExecutionContext::current();
 return ownerTask&&ctx&&ctx->running(ctx->id())?ctx->id():0;
}
static bool fakeVideoStart(t5_video_surface_v1*,uint8_t){engineRunning=startOkay;scanCounter=0;drivePending=false;return startOkay;}
static bool fakeVideoSubmit(uint16_t,uint16_t){if(!engineRunning||drivePending||!submitOkay)return false;++submissions;drivePending=true;return true;}
static bool fakeVideoCanSubmit(){return engineRunning&&!drivePending;}
static bool fakeVideoPending(){return drivePending;}
static uint32_t fakeVideoCounter(){return scanCounter;}
static bool fakeVideoStop(){if(!stopOkay)return false;engineRunning=drivePending=false;return true;}
static t5_video_api_v1 fast={1,sizeof(fast),nullptr,nullptr,fakeVideoCanSubmit,fakeVideoSubmit,fakeVideoPending,fakeVideoCounter,nullptr,fakeVideoStart,nullptr,nullptr,fakeVideoStop};
static t5_display_provider_api_v1 displayProvider{};
static const t5_display_provider_api_v1*platformDisplayProvider(){displayProvider.fast=&fast;return &displayProvider;}
void nativeVideoServiceTouchPresentation();
namespace VideoFacade{bool nativeVideoForceStop();void nativeVideoServiceTouchPresentation();}
'''
base=f.strings['prefix']+f.consumer+f.provider+video+f.prefix+f.session+f.rest+f.bodies
base=base.replace('static void nativeVideoServiceTouchPresentation(){}','')
base=base.replace('static void nativeStreamsBegin(){}static void nativeStreamsEnd(){}','static void nativeStreamsBegin(){assert(appContext.begin());}static void nativeStreamsEnd(){appContext.end();}')
base += '\n' + f.method(f.host, 'bool touchContact(') + '\n' + f.method(f.host, 'bool takeTouchSwipe(')
base = base.replace(' void clearInjectedButtonTap(){}', ' void clearInjectedButtonTap(){}\n bool getTouchContact(TouchPoint&p,GfxRenderer&){return nativeTouchGetContact(p);}\n bool getTouchSwipe(TouchPoint&a,TouchPoint&b,GfxRenderer&){return nativeTouchGetSwipe(a,b);}')
base+='\nnamespace VideoFacade{\n'+bridge+'\n}\nvoid nativeVideoServiceTouchPresentation(){VideoFacade::nativeVideoServiceTouchPresentation();}\n'
tests=r'''
static void tap(){queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();nowMs+=20;queue(RISC_TOUCH_EVENT_UP);serviceProvider();}
static void beginSurface(){nativeTouchBeginSurfaceTransition();serviceProvider();}
static void scanStart(){++scanCounter;} // Real backend increments at START.
static void scanComplete(){drivePending=false;} // Real backend waits for all rows/DMA first.
int main(){
 api=&provider;subscription=1;nowMs=1000;GfxRenderer r;MappedInputManager input;t5_app_input_t out{};
 const auto*v=VideoFacade::t5_video_get_api(1);assert(v);
 launchHook=[&]{
  assert(nativeProviderStreamConsumer());assert(v->start_format(nullptr,T5_VIDEO_PIXEL_GRAY_2BPP_MSB));
  r.backing.ready=false; // Takeover forbids every ordinary renderer present.
  r.displayBuffer();assert(surfaceTransitionPending);
  // Start, rejected submit, accepted queue and scan-counter movement all fail
  // to prove the app frame has reached the panel.
  assert(surfaceTransitionPending);submitOkay=false;assert(!v->submit(0,0));
  assert(pollInput(&out,0,false)&&surfaceTransitionPending&&!out.tapped);
  submitOkay=true;assert(v->submit(0,0));tap();
  assert(pollInput(&out,0,false)&&surfaceTransitionPending&&!out.tapped);
  scanStart();tap();assert(pollInput(&out,0,false)&&surfaceTransitionPending&&!out.tapped);
  // Static Springboard makes no more video calls after submit. Its normal
  // input poll must acknowledge the settled frame and discard old/held input.
  queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();scanComplete();
  assert(pollInput(&out,0,false)&&!surfaceTransitionPending&&!out.tapped);serviceProvider();
  NativeTouchPoint contact{};assert(!nativeTouchGetContact(contact));
  queue(RISC_TOUCH_EVENT_UP);serviceProvider();assert(pollInput(&out,0,false)&&!out.tapped);
  tap();assert(pollInput(&out,0,false)&&out.tapped);
  // Contact-only and swipe-only clients also observe a completed static frame
  // without another submit or ordinary renderer call.
  beginSurface();assert(v->submit(0,0));scanStart();scanComplete();
  t5_app_contact_t contactState{};assert(touchContact(&contactState)&&!surfaceTransitionPending&&!contactState.down);
  serviceProvider();queue(RISC_TOUCH_EVENT_DOWN);serviceProvider();
  assert(touchContact(&contactState)&&contactState.down);queue(RISC_TOUCH_EVENT_UP);serviceProvider();
  beginSurface();assert(v->submit(0,0));scanStart();scanComplete();
  t5_app_swipe_t swipe{};assert(!takeTouchSwipe(&swipe)&&!surfaceTransitionPending);serviceProvider();
  queue(RISC_TOUCH_EVENT_DOWN);queue(RISC_TOUCH_EVENT_MOVE,180);queue(RISC_TOUCH_EVENT_UP,180);serviceProvider();
  assert(takeTouchSwipe(&swipe));
  // Counter without a settled/running engine never opens coordinates.
  beginSurface();assert(v->submit(0,0));scanStart();drivePending=false;engineRunning=false;
  assert(pollInput(&out,0,false)&&surfaceTransitionPending&&!out.tapped);
  engineRunning=true;assert(pollInput(&out,0,false)&&!surfaceTransitionPending);serviceProvider();
  // Completion captured for the outgoing epoch cannot open a newer surface.
  beginSurface();assert(v->submit(0,0));scanStart();beginSurface();scanComplete();
  assert(pollInput(&out,0,false)&&surfaceTransitionPending);
  assert(v->submit(0,0));scanStart();scanComplete();assert(pollInput(&out,0,false)&&!surfaceTransitionPending);serviceProvider();
  // Restart and even failed teardown cancel prior scan evidence.
  beginSurface();assert(v->submit(0,0));assert(v->start_format(nullptr,T5_VIDEO_PIXEL_GRAY_2BPP_MSB));
  scanStart();scanComplete();assert(pollInput(&out,0,false)&&surfaceTransitionPending);
  assert(v->submit(0,0));scanStart();stopOkay=false;assert(!VideoFacade::nativeVideoForceStop());scanComplete();
  assert(pollInput(&out,0,false)&&surfaceTransitionPending);stopOkay=true;
  assert(v->submit(0,0));scanStart();scanComplete();assert(pollInput(&out,0,false)&&!surfaceTransitionPending);serviceProvider();
  // Foreign owner cannot inherit readiness, even within a retained provider.
  beginSurface();assert(v->submit(0,0));scanStart();scanComplete();ownerTask=false;
  nativeVideoServiceTouchPresentation();ownerTask=true;assert(pollInput(&out,0,false)&&surfaceTransitionPending);
  assert(v->submit(0,0));scanStart();scanComplete();borrowed=false;
  assert(pollInput(&out,0,false)&&surfaceTransitionPending);borrowed=true;
  assert(v->submit(0,0));scanStart();scanComplete();assert(pollInput(&out,0,false)&&!surfaceTransitionPending);serviceProvider();
  // Existing invocation IDs are generation-safe; a replacement invocation
  // cannot use an old pending completion even on the same task and touch epoch.
  beginSurface();assert(v->submit(0,0));scanStart();scanComplete();
  appContext.end();assert(appContext.begin());assert(pollInput(&out,0,false)&&surfaceTransitionPending);
  assert(v->submit(0,0));scanStart();scanComplete();assert(pollInput(&out,0,false)&&!surfaceTransitionPending);serviceProvider();
  // With first video surface ready, the 30s stale-tail guard still applies.
  tap();assert(pollInput(&out,0,false)&&out.tapped);nowMs+=30000;tap();
  assert(pollInput(&out,0,false)&&!out.tapped);serviceProvider();tap();assert(pollInput(&out,0,false)&&out.tapped);
  assert(VideoFacade::nativeVideoForceStop());r.backing.ready=true;
 };
 assert(runNativeApp("/sd/Apps/app.elf",r,input)==ESP_OK);
 assert(!engineRunning&&surfaceTransitionPending);
 puts("Actual video-only native UI: queued/partial/failure stay fenced; settled frame, fresh touch, owner/epoch/restart/stop and 30s replay guards PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 cpp=Path(d)/'test.cpp';binary=Path(d)/'test';cpp.write_text(base+tests)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
  '-Wno-unused-parameter','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-omit-frame-pointer',
  '-I'+str(ROOT/'src'),'-I'+str(ROOT/'src/native'),'-I'+str(ROOT/'sdk/driver'),'-I'+str(ROOT/'lib/DisplaySurface'),
  '-I'+str(ROOT/'lib/NativeApps/include'),str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'},check=True,timeout=15)
