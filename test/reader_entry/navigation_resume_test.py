#!/usr/bin/env python3
"""Keep child Home/navigation state across Reader readmission and Home resume."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
host=(ROOT/'src/native/NativeAppHost.cpp').read_text()
home=(ROOT/'src/activities/home/HomeActivity.cpp').read_text()
prelude=host[host.index('  lastLaunchError.clear();',host.index('static esp_err_t runNativeAppImpl(')):host.index('  // Legacy loose ELFs',host.index('static esp_err_t runNativeAppImpl('))]
springboard=host[host.index('bool runNativeSpringboard('):]
# It is the final function in this source file; compile the production body.
apps=home[home.index('  if (appsPending) {',home.index('void HomeActivity::loop()')):home.index('  if (firstRenderDone',home.index('void HomeActivity::loop()'))]
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=-1,UI_12_FONT_ID=12;
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
uint32_t millis(){return 0;}
bool idleSleepRequested(){return false;}
bool serviceIdleSleep(bool){return false;}
void nativeTouchBeginSurfaceTransition(bool =false){}
bool nativeTouchHadActivity(){return false;}
struct {unsigned buttons=0;} frame;
auto nativeNavigationFrame(){return frame;}
enum class DisplayPresentMode{Clean};
struct RenderLock{};
struct GfxRenderer{void clearScreen(){} void drawText(int,int,int,const char*){} void displayBuffer(DisplayPresentMode){}};
struct MappedInputManager{struct TouchPoint{};void update(){} bool wasTouchTapped(TouchPoint&,GfxRenderer&){return true;} bool wasAnyPressed(){return false;} bool wasAnyReleased(){return false;} bool wasTouchHomeButtonPressed(){return false;}};
static std::string lastLaunchError,queuedLaunch;
static bool homeRequested=false,firmwareActionPending=false,session=false;
namespace StartupScreen{void app(GfxRenderer&,const char*,const char*){}}
static int launches=0,resolutions=0;
bool risc_runtime_retention_required(){return false;}
bool native_app_loader_retained(){return false;}
void esp_task_wdt_reset(){} void delay(unsigned){}
struct{bool ready(){return true;}bool exists(const char*){return true;}} Storage;
namespace RuntimePackages{bool recoverAppInventory(){return true;}bool recoverAppPair(const char*){return true;}}
bool resolveInstalledAppPath(const char*,std::string& path){++resolutions;path="/sd/Apps/springboard.elf";return true;}
bool t5_safe_elf_name(const char*){return true;}
'''
source=prefix+'\nesp_err_t prepareLaunch(bool readerEntry){\n'+prelude+'return ESP_OK;\n}\n'
source+=r'''
int runNativeApp(const char*,GfxRenderer&,MappedInputManager&){++launches;assert(prepareLaunch(false)==ESP_OK);return ESP_OK;}
'''+springboard+r'''
struct{bool deferNativeAppLoop(void*){return false;}}activityManager;
struct Home {
 GfxRenderer renderer;MappedInputManager mappedInput;
 bool appsPending=true,appsResume=true;
 void loadHomeApps(){}void requestUpdate(){}
 void step(){
'''+apps+r'''
 }
};
int main(){
 // A suspended Springboard firmware child requests Home. Fresh Reader entry
 // must preserve that request until Home's existing Apps branch consumes it.
 homeRequested=true;firmwareActionPending=true;queuedLaunch="child result";
 assert(prepareLaunch(true)==ESP_OK);
 assert(homeRequested&&firmwareActionPending&&queuedLaunch=="child result");
 Home home;home.step();
 assert(!home.appsPending&&!home.appsResume&&launches==0&&resolutions==0);
 // Without Home, the same old parent resumes normally, then a real child
 // launch creates its own clean session/navigation state.
 homeRequested=false;Home resumed;resumed.step();
 assert(launches==1&&resolutions==1&&!resumed.appsPending);
 assert(!homeRequested&&!firmwareActionPending&&queuedLaunch.empty());
 puts("Production Reader prelude/Home Apps/Springboard: child Home return and ordinary resume preserve navigation PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-navigation-') as directory:
    temp=Path(directory);cpp=temp/'navigation.cpp';binary=temp/'navigation'
    cpp.write_text(source)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-variable',str(cpp),'-o',str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=5)
