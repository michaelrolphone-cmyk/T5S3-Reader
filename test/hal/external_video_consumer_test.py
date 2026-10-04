#!/usr/bin/env python3
"""Legacy takeover guard also stops fast work started by a portable client."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/native/NativeVideoBridge.cpp').read_text()
source=re.sub(r'^#include .*\n','',source,flags=re.M)
source=source[source.index('extern "C" bool'):source.index('\n#else')]
prefix=r'''
#include <cassert>
#include <cstdio>
#include <T5VideoApi.h>
#include <T5DisplayProviderV1.h>
static bool borrowed=false,available=true,stopOkay=true;
static unsigned starts,stops;
bool nativeVideoForceStop();
static uint32_t nativeProviderStreamConsumer(){return 0;}
static bool nativeTouchAwaitingSurfacePresentation(uint32_t&){return false;}
static void nativeTouchSurfacePresented(uint32_t,bool){}
extern "C" bool native_hardware_display_is_borrowed(){return borrowed;}
static bool startEngine(t5_video_surface_v1*,uint8_t){++starts;return true;}
static bool stopEngine(){++stops;return stopOkay;}
static t5_video_api_v1 fast={1,sizeof(fast),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,startEngine,nullptr,nullptr,stopEngine};
static t5_display_provider_api_v1 provider={};
const t5_display_provider_api_v1*platformDisplayProvider(){provider.fast=&fast;return available?&provider:nullptr;}
'''
main=r'''
int main(){
 auto*api=t5_video_get_api(1);assert(api&&!t5_video_get_api(99));
 assert(!api->start(nullptr)&&!starts);assert(nativeVideoForceStop()&&!stops);
 borrowed=true;available=false;assert(!api->start(nullptr));assert(!nativeVideoForceStop());
 available=true;assert(nativeVideoForceStop()&&stops==1); // portable path: no legacy start
 assert(api->start(nullptr)&&starts==1);stopOkay=false;
 assert(!nativeVideoForceStop());stopOkay=true;assert(nativeVideoForceStop());
 borrowed=false;assert(!api->start(nullptr));
 puts("Legacy display consumer: takeover authority, portable-client force-stop, failed cleanup retained PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(prefix+source+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(root/'sdk/driver'),'-I'+str(root/'lib/NativeApps/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=10)
