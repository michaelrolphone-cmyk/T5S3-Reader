from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'src/native/NativeHardwareTakeover.cpp').read_text()
body=s[s.index('extern "C" bool native_hardware_display_is_borrowed'):]
prefix=r'''
#include <cassert>
#include <cstdio>
#include "T5HardwareTakeover.h"
using esp_err_t=int;
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_FAIL 3
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static bool s_display_borrowed=false,s_touch_borrowed=false,stop_ok=true;
static unsigned suspends,resumes,reclaims,restores;
static struct {int backlightLevel=4;} SETTINGS;
struct HalDisplay {enum {FULL_REFRESH};};
struct Display {
 bool suspendForExternalOwner(){return true;}
 bool resumeFromExternalOwner(){++restores;return true;}
 void requestNextRefresh(int){}
} display;
namespace Board {void restoreBacklightLevel(int){}}
bool nativeTouchAvailable(){return true;}
bool nativeTouchSuspend(){++suspends;return true;}
bool nativeTouchResume(){++resumes;return true;}
bool nativeVideoForceStop(){return stop_ok;}
void native_app_memory_end(){++reclaims;}
'''
main=r'''
int main(){
 const unsigned ui=T5_HARDWARE_TAKEOVER_DISPLAY|T5_HARDWARE_TAKEOVER_UI_VIDEO;
 assert(native_hardware_takeover_begin(T5_HARDWARE_TAKEOVER_UI_VIDEO)==ESP_ERR_NOT_SUPPORTED);
 assert(native_hardware_takeover_begin(ui)==ESP_OK);
 assert(s_display_borrowed && suspends==0);
 assert(native_hardware_takeover_end(ui)==ESP_OK);
 assert(!s_display_borrowed && resumes==0 && reclaims==1 && restores==1);
 assert(native_hardware_takeover_begin(T5_HARDWARE_TAKEOVER_DISPLAY)==ESP_OK);
 assert(suspends==1);
 assert(native_hardware_takeover_end(T5_HARDWARE_TAKEOVER_DISPLAY)==ESP_OK);
 assert(resumes==1);
 assert(native_hardware_takeover_begin(ui)==ESP_OK);stop_ok=false;
 assert(native_hardware_takeover_end(ui)==ESP_ERR_INVALID_STATE);
 assert(s_display_borrowed && reclaims==2 && restores==2);
 puts("UI video takeover preserves capture; legacy suspension and unsafe-teardown retention PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'test.cpp').write_text(prefix+body+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT/'lib/NativeApps/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
