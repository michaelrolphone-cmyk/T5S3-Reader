#!/usr/bin/env python3
"""Run production handoff loops against the real generation/owner guards.

Only platform/rendering and child ELF execution are mocked. The test compiles
unchanged complete loop bodies, so flags/results cannot be consumed before
unload or silently replaced with a new test-only dispatcher.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        if text[end] == '{':
            depth += 1
        elif text[end] == '}':
            depth -= 1
        end += 1
    return text[start:end]


prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "native/NativeReaderEntryState.h"
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1;
struct GfxRenderer {};
struct MappedInputManager {
 enum class Button {Back,Confirm};
 bool back=false;
 bool wasPressed(Button b) {return back && b==Button::Back;}
};
struct Activity {
 GfxRenderer renderer; MappedInputManager mappedInput;
 bool finished=false; int updates=0;
 virtual ~Activity()=default;
 virtual void loop()=0;
 void finish(){finished=true;}
 void requestUpdate(){++updates;}
};
namespace NativeReaderEntry {
State state;
bool mapped(){return state.mapped();}
bool deferActivity(Activity* a){return state.request(Action::ActivityLoop,a);}
}
struct ActivityManager {
 enum class PendingAction {None,Push};
 std::unique_ptr<Activity> currentActivity;
 PendingAction pendingAction=PendingAction::None;
 Activity* deferredActivity=nullptr;
 uint64_t activityGeneration=1, deferredGeneration=0;
 int finishes=0;
 bool deferNativeAppLoop(Activity*);
 bool resumeNativeAppLoop(Activity*);
 void finishLoop(){++finishes;}
} activityManager;
struct Result {bool available=false; int error=0; uint64_t cookie=0; bool cancelled=false;};
Result launchState,openResult,viewerResult,keyboardState,wifiState,confirmationState;
std::string activeSourcePath;
constexpr const char* kViewerPath="/sd/Apps/image_viewer.elf";
std::vector<std::string> launches;
std::deque<int> errors;
int runNativeApp(const char* path,GfxRenderer&,MappedInputManager&) {
 assert(!NativeReaderEntry::mapped());
 launches.emplace_back(path);
 if(errors.empty())return ESP_OK;
 int e=errors.front();errors.pop_front();return e;
}
'''
manager = (ROOT/'src/activities/ActivityManager.cpp').read_text()
source = prefix + '\n' + block(manager, 'bool ActivityManager::deferNativeAppLoop(')
source += '\n' + block(manager, 'bool ActivityManager::resumeNativeAppLoop(')
bridges = {
    'NativeChildElfActivity': 'NativeFileBrowserBridge.cpp',
    'NativeDeleteConfirmationActivity': 'NativeFileBrowserBridge.cpp',
    'NativeFileOpenActivity': 'NativeFileOpenBridge.cpp',
    'NativeImageViewerActivity': 'NativeImageBridge.cpp',
    'NativeKeyboardActivity': 'NativeSystemUiBridge.cpp',
    'NativeWifiActivity': 'NativeSystemUiBridge.cpp',
    'NativeSettingsActionActivity': 'NativeSettingsBridge.cpp',
}
for name, filename in bridges.items():
    text = (ROOT/'src/native'/filename).read_text()
    text = text[text.index('class '+name):]
    loop = block(text, 'void loop() override')
    source += '\nstruct '+name+r''' : Activity {
 std::string resumePath="/sd/Apps/parent.elf",childPath="/sd/Apps/child.elf";
 std::string targetPath="/sd/Apps/target.elf",sourcePath="/sd/books/test.epub";
 uint64_t cookie=17;
 bool resumeReturned=false,ranChild=false,ranTarget=false,ranViewer=false,childCompleted=false;
 ''' + loop + '\n};\n'

text = (ROOT/'src/activities/settings/FontSelectionActivity.cpp').read_text()
source += r'''struct FontSelectionActivity : Activity {
 bool launchAttempted=false,launchFailed=false;
 void loop() override;
};
''' + block(text, 'void FontSelectionActivity::loop()')
source += r'''
template<class T> T& select(){
 assert(!NativeReaderEntry::mapped());
 activityManager.currentActivity=std::make_unique<T>();
 ++activityManager.activityGeneration;
 activityManager.pendingAction=ActivityManager::PendingAction::None;
 activityManager.deferredActivity=nullptr;
 launches.clear();errors.clear();
 launchState=openResult=viewerResult=keyboardState=wifiState=confirmationState={};
 return static_cast<T&>(*activityManager.currentActivity);
}
void enter(){assert(NativeReaderEntry::state.begin());}
void dispatch(Activity& a){
 assert(NativeReaderEntry::state.pending());
 NativeReaderEntry::state.end(true);
 auto work=NativeReaderEntry::state.take();
 assert(work.activity==&a);
 assert(activityManager.resumeNativeAppLoop(&a));
 assert(!activityManager.resumeNativeAppLoop(&a)); // Exactly once.
}
template<class T> void chain(){
 auto& a=select<T>();enter();a.loop();
 assert(launches.empty()&&!a.ranChild&&!a.ranTarget&&!a.ranViewer&&!a.resumeReturned);
 dispatch(a);
 assert(launches.size()==2&&launches.back()==a.resumePath&&a.resumeReturned);
 assert(!a.finished);
 enter();a.loop();assert(a.finished&&!NativeReaderEntry::state.pending());
 NativeReaderEntry::state.end(true);NativeReaderEntry::state.take();
 // Child failure still returns its original error to the parent synchronously.
 auto& b=select<T>();errors={ESP_FAIL,ESP_OK};enter();b.loop();dispatch(b);
 assert(launches.size()==2&&b.resumeReturned&&!b.finished);
 // Parent failure clears the result and closes the wrapper.
 auto& c=select<T>();errors={ESP_OK,ESP_FAIL};enter();c.loop();dispatch(c);
 assert(launches.size()==2&&c.finished&&!c.resumeReturned);
}
template<class T> void completion(Result& result){
 auto& a=select<T>();enter();a.loop();
 assert(!NativeReaderEntry::state.pending()&&!a.childCompleted&&launches.empty());
 // A cancelled keyboard/Wi-Fi/picker result survives unload until its parent consumes it.
 result.available=true;result.cancelled=true;result.cookie=17;a.childCompleted=true;
 a.loop();assert(a.childCompleted&&result.available&&result.cancelled&&launches.empty());
 dispatch(a);assert(!a.childCompleted&&a.resumeReturned&&!a.finished&&launches.size()==1);
 assert(result.available&&result.cancelled&&result.cookie==17);
 enter();a.loop();assert(a.finished&&!NativeReaderEntry::state.pending());
 NativeReaderEntry::state.end(true);NativeReaderEntry::state.take();
 auto& b=select<T>();b.childCompleted=true;errors={ESP_FAIL};result.available=true;
 enter();b.loop();dispatch(b);assert(b.finished&&!b.resumeReturned);
}
int main(){
 chain<NativeChildElfActivity>();chain<NativeFileOpenActivity>();chain<NativeImageViewerActivity>();
 completion<NativeKeyboardActivity>(keyboardState);completion<NativeWifiActivity>(wifiState);
 completion<NativeDeleteConfirmationActivity>(confirmationState);
 completion<NativeSettingsActionActivity>(confirmationState);
 auto& a=select<FontSelectionActivity>();enter();a.loop();
 assert(!a.launchAttempted&&launches.empty());dispatch(a);
 assert(a.launchAttempted&&a.finished&&launches.size()==1);
 auto& b=select<FontSelectionActivity>();errors={ESP_FAIL};enter();b.loop();dispatch(b);
 assert(b.launchAttempted&&b.launchFailed&&!b.finished);
 enter();b.mappedInput.back=true;b.loop();assert(b.finished&&!NativeReaderEntry::state.pending());
 NativeReaderEntry::state.end(true);NativeReaderEntry::state.take();
 // A changed generation, a different pointer, or a newer pending navigation
 // invalidates the old request without running any child.
 for(int reason=0;reason<3;++reason){
  auto& c=select<FontSelectionActivity>();enter();c.loop();
  NativeReaderEntry::state.end(true);NativeReaderEntry::state.take();
  FontSelectionActivity other;
  if(reason==0)++activityManager.activityGeneration;
  if(reason==1)activityManager.pendingAction=ActivityManager::PendingAction::Push;
  assert(!activityManager.resumeNativeAppLoop(reason==2?&other:&c));
  assert(launches.empty()&&!c.launchAttempted);
 }
 puts("Production activity loops: child/parent chains, cancel/error results, pre-mutation guards, generation and exactly-once dispatch PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-activities-') as directory:
    temp=Path(directory); cpp=temp/'activities.cpp'; binary=temp/'activities'
    cpp.write_text(source)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True,timeout=45)
    subprocess.run([str(binary)],check=True,timeout=30)
