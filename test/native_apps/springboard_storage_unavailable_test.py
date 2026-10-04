#!/usr/bin/env python3
"""Run the exact firmware Apps launch path with storage/UI/loader fixtures."""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=ROOT/'src/native/NativeAppHost.cpp')
args = parser.parse_args()
source = args.source.read_text()
start = source.index('bool runNativeSpringboard(')
brace = source.index('{', start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
method = source[start:end]
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
static unsigned long nowMs;
unsigned long millis(){return nowMs;}
void delay(unsigned n){nowMs+=n;assert(nowMs<10000);}
void esp_task_wdt_reset(){}
template<class... T>void logStub(T...){}
#define LOG_INF(...) logStub(__VA_ARGS__)
#define LOG_ERR(...) logStub(__VA_ARGS__)
struct RenderLock{~RenderLock(){}};
constexpr int UI_12_FONT_ID=12;
enum class DisplayPresentMode {Clean};
struct GfxRenderer {
 std::string message;unsigned frames=0;
 void clearScreen(){message.clear();}
 void drawText(int,int,int y,const char* s){if(y==100||y==136){if(!message.empty())message+=' ';message+=s;}}
 void displayBuffer(DisplayPresentMode){++frames;}
};
struct MappedInputManager {
 struct TouchPoint{};
 enum class Dismiss {Button,Touch,Home,Idle};
 Dismiss dismiss=Dismiss::Button;unsigned updates=0;
 void update(){++updates;}
 bool wasAnyPressed(){return dismiss==Dismiss::Button;}
 bool wasAnyReleased(){return false;}
 bool wasTouchTapped(TouchPoint&,GfxRenderer&){return dismiss==Dismiss::Touch;}
 bool wasTouchHomeButtonPressed(){return dismiss==Dismiss::Home;}
};
namespace StartupScreen {void app(GfxRenderer&,const char*,const char*){}}
static bool sleepRequested,homeRequested,firmwareActionPending,idleOnError;
bool idleSleepRequested(){return sleepRequested;}
bool serviceIdleSleep(bool){return idleOnError;}
struct Nav{unsigned buttons=0;};
Nav nativeNavigationFrame(){return {};}
bool nativeTouchHadActivity(){return false;}
static std::string queuedLaunch,lastLaunchError;
struct StorageFixture {
 bool mounted=true,sidecar=true,dropOnSidecar=false;
 unsigned existsCalls=0;
 bool ready()const{return mounted;}
 bool exists(const char* p){++existsCalls;if(!std::strcmp(p,"/Apps"))return mounted;
   if(dropOnSidecar)mounted=false;return mounted&&sidecar;}
} Storage;
static unsigned inventoryCalls,resolveCalls,loaderCalls;
static bool resolved=true,dropOnResolve,dropOnInventory,dropOnRun,queueChild;
static bool pairOkay=true,dropOnPair;
static int loaderResult=0,childResult=0;
constexpr int ESP_OK=0;
namespace RuntimePackages {
 bool recoverAppInventory(){++inventoryCalls;if(dropOnInventory)Storage.mounted=false;return Storage.mounted;}
 bool recoverAppPair(const char*){if(dropOnPair)Storage.mounted=false;return pairOkay&&Storage.mounted;}
}
bool t5_safe_elf_name(const char*){return true;}
bool resolveInstalledAppPath(const char*,std::string& out){++resolveCalls;if(dropOnResolve)Storage.mounted=false;
 if(!Storage.mounted||!resolved)return false;out="/sd/Apps/springboard/springboard.elf";return true;}
int runNativeApp(const char*,GfxRenderer&,MappedInputManager&){++loaderCalls;
 if(dropOnRun)Storage.mounted=false;
 if(loaderCalls==1){queuedLaunch=queueChild?"/sd/Apps/settings/settings.elf":"";return loaderResult;}
 queueChild=false;queuedLaunch.clear();if(childResult)Storage.mounted=false;return childResult;
}
'''
tests = r'''
static void reset(GfxRenderer& r,MappedInputManager& i){r={};i={};Storage={};nowMs=0;
 sleepRequested=homeRequested=firmwareActionPending=idleOnError=false;
 queuedLaunch.clear();lastLaunchError.clear();inventoryCalls=resolveCalls=loaderCalls=0;
 resolved=true;dropOnResolve=dropOnInventory=dropOnRun=queueChild=dropOnPair=false;
 pairOkay=true;loaderResult=childResult=0;}
static void unavailable(const GfxRenderer& r){assert(r.message=="SD card unavailable. Check card and matching SD files.");}
int main(){GfxRenderer r;MappedInputManager i;
 // Initially unavailable: no recovery, resolution, loader or misleading install prompt.
 for(auto dismiss:{MappedInputManager::Dismiss::Button,MappedInputManager::Dismiss::Touch,MappedInputManager::Dismiss::Home,MappedInputManager::Dismiss::Idle}){
  reset(r,i);Storage.mounted=false;i.dismiss=dismiss;idleOnError=dismiss==MappedInputManager::Dismiss::Idle;
  assert(!runNativeSpringboard(r,i,false));unavailable(r);assert(!inventoryCalls&&!resolveCalls&&!loaderCalls&&i.updates==1);
 }
 // A normal missing launcher keeps its existing installation message.
 reset(r,i);resolved=false;assert(!runNativeSpringboard(r,i,false));
 assert(r.message=="Install Springboard or copy springboard.elf and .json to /Apps."&&resolveCalls==1&&!loaderCalls);
 // Failures during recovery, resolution and ELF load are storage failures too.
 reset(r,i);dropOnInventory=true;assert(!runNativeSpringboard(r,i,false));unavailable(r);assert(!resolveCalls&&!loaderCalls);
 reset(r,i);dropOnResolve=true;assert(!runNativeSpringboard(r,i,false));unavailable(r);assert(!loaderCalls);
 reset(r,i);dropOnRun=true;loaderResult=-1;assert(!runNativeSpringboard(r,i,false));unavailable(r);
 // Genuine loader failures retain their existing precise message while SD is ready.
 reset(r,i);loaderResult=-1;lastLaunchError="Existing loader failure.";assert(!runNativeSpringboard(r,i,false));assert(r.message==lastLaunchError);
 // Returning from a selected app must recheck storage; child recovery/manifest/load failures do not imply deletion.
 reset(r,i);queueChild=true;dropOnPair=true;assert(!runNativeSpringboard(r,i,false));unavailable(r);assert(loaderCalls==1);
 reset(r,i);queueChild=true;Storage.dropOnSidecar=true;assert(!runNativeSpringboard(r,i,false));unavailable(r);assert(loaderCalls==1);
 reset(r,i);queueChild=true;childResult=-1;assert(!runNativeSpringboard(r,i,false));unavailable(r);assert(loaderCalls==2);
 // Successful retry follows the original path; no automatic mount, write or reset is introduced.
 reset(r,i);Storage.mounted=false;assert(!runNativeSpringboard(r,i,false));unavailable(r);
 Storage.mounted=true;r.message.clear();assert(!runNativeSpringboard(r,i,false));assert(r.message.empty()&&loaderCalls==1&&resolveCalls==1);
 reset(r,i);assert(!runNativeSpringboard(r,i,false));assert(r.message.empty()&&loaderCalls==1&&inventoryCalls==1);
 // Existing sleep/resume fences still avoid all UI/storage/loader work.
 reset(r,i);sleepRequested=true;assert(!runNativeSpringboard(r,i,false));assert(!Storage.existsCalls&&!r.frames&&!loaderCalls);
 reset(r,i);homeRequested=true;assert(!runNativeSpringboard(r,i,true));assert(!Storage.existsCalls&&!r.frames&&!loaderCalls);
 std::puts("actual Apps launch: unavailable/missing, mid-operation failure, dismissal and retry PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='apps-storage-') as temp:
    cpp=Path(temp)/'test.cpp';cpp.write_text(prefix+method+tests)
    for sanitize in (False,True):
        binary=Path(temp)/('sanitized' if sanitize else 'normal')
        flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitize else []
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',*flags,str(cpp),'-o',str(binary)],check=True,timeout=60)
        subprocess.run([str(binary)],check=True,timeout=30)
