#!/usr/bin/env python3
"""Exercise the actual paper loop entry and original Home/book boot callback."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/main.cpp').read_text()
start=source[source.index('void prepareReaderApplication('):source.index('\nvoid setup() {',source.index('static void startReaderApplication()'))]
loop=source[source.rindex('void loop() {'):source.index('\n#endif // RISCRTE_PROFILE_HEADLESS')]
prefix=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "native/NativeReaderEntryState.h"
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
static int pumps=0,entryCalls=0,childCalls=0,resolveCalls=0,delays=0;
static bool found=true,retained=false,bootReady=true,savedPolicy=false,rejectEntry=false;
bool shouldResumeReaderOnBoot(){return savedPolicy;}
bool x4ReaderStartupReady(){return bootReady;}
bool x4DiagnosticLoop(){return false;}
static int scheduled=0;void x4ReaderActivityScheduled(){++scheduled;}
static std::string selected,book;
struct HalDisplay {enum RefreshMode {HALF_REFRESH};};
struct RenderLock{};
struct {void suppressInitialFullRefresh(){}} display;
struct {void requestNextRefresh(HalDisplay::RefreshMode){}} renderer;
struct {} mappedInputManager;
struct {std::string openEpubPath="/saved.epub";int readerActivityLoadCount=0,saves=0;void saveToFile(){++saves;}} APP_STATE;
namespace StartupScreen {static int fades=0;void armBootFade(){++fades;}}
struct {void requestUpdate(bool){}int homes=0,reads=0;void goHome(){++homes;}void goToReader(std::string p,int){++reads;book=p;}} activityManager;
int readerResumeRefreshMode(){return 0;}
static bool g_displayBootFailed=false,g_readerStartPending=true,g_readerResumeOnBoot=false;
static bool g_readerDeskClockWake=false,g_readerEntryEligible=true;
namespace RuntimeDefaultApp {
enum class Selection {Absent,Ready,Invalid};
static Selection choice=Selection::Absent;
Selection read(char* out){std::strcpy(out,selected.c_str());return choice;}
}
bool resolveInstalledAppPath(const char* artifact,std::string& path){++resolveCalls;path="/sd/Apps/default/"+std::string(artifact);return found;}
void delay(int){++delays;}
static void readerApplicationLoop(){++pumps;}
static void runReaderSleep(NativeReaderEntry::Action,bool){}
namespace NativeReaderEntry {
bool blocked(){return retained;}
template<class R,class I> bool run(const char* path,R&,I&,void(*start)(),void(*loop)(),void(*)(Action,bool)){
 ++entryCalls;assert(std::string(path)=="/sd/Apps/default/default.elf");if(rejectEntry)return false;start();loop();return false;
}
}
template<class R,class I> int runNativeApp(const char*,R&,I&){++childCalls;return 0;}
'''
tests=r'''
int main(int argc,char**argv){
 assert(argc==2);std::string scenario=argv[1];
 if(scenario=="reader"||scenario=="desk-reader")savedPolicy=true;
 if(scenario=="desk"||scenario=="desk-reader")g_readerDeskClockWake=true;
 prepareReaderApplication(g_readerDeskClockWake);
 assert(g_readerResumeOnBoot==savedPolicy);
 savedPolicy=false; // Captured boot decision survives later state changes.
 if(scenario=="notready")bootReady=false;
 if(scenario=="rejected")rejectEntry=true;
 if(scenario=="missing")found=false;
 if(scenario=="invalid")RuntimeDefaultApp::choice=RuntimeDefaultApp::Selection::Invalid;
 if(scenario=="explicit"){selected="default.elf";RuntimeDefaultApp::choice=RuntimeDefaultApp::Selection::Ready;}
 if(scenario=="custom"){selected="custom.elf";RuntimeDefaultApp::choice=RuntimeDefaultApp::Selection::Ready;}
 if(scenario=="recovery"){g_readerEntryEligible=false;g_readerStartPending=false;}
 if(scenario=="display")g_displayBootFailed=true;
 if(scenario=="retained")retained=true;
 loop();loop();
 if(scenario=="notready"){
  assert(!resolveCalls&&!entryCalls&&!pumps&&!scheduled);
 }else if(scenario=="display"||scenario=="retained"){
  assert(!resolveCalls&&!entryCalls&&!pumps&&delays==2);
 }else if(scenario=="recovery"){
  assert(!resolveCalls&&!entryCalls&&pumps==2&&!activityManager.homes&&!activityManager.reads);
 }else if(scenario=="custom"){
  assert(childCalls==1&&!entryCalls&&resolveCalls==1&&activityManager.homes==2);
 }else{
  assert(!childCalls&&entryCalls==(scenario!="missing"&&scenario!="invalid"));
  assert(resolveCalls==(scenario!="invalid"));
  assert(!g_readerStartPending);
  if(scenario=="reader"||scenario=="desk-reader"){
   assert(activityManager.reads==1&&!activityManager.homes&&book=="/saved.epub");
   assert(APP_STATE.openEpubPath.empty()&&APP_STATE.readerActivityLoadCount==1&&APP_STATE.saves==1);
   assert(!StartupScreen::fades);
  }else{
   assert(activityManager.homes==1&&!activityManager.reads);
#ifdef BOARD_XTEINK_X4_PRO
   assert(!StartupScreen::fades&&scheduled==1);
#else
   assert(StartupScreen::fades==(scenario!="desk"));
#endif
  }
 }
 printf("Production paper boot/default dispatch %s PASS\n",scenario.c_str());
}
'''
with tempfile.TemporaryDirectory(prefix='reader-boot-') as directory:
    temp=Path(directory);cpp=temp/'boot.cpp';binary=temp/'boot'
    cpp.write_text(prefix+start+loop+tests)
    for board in ("BOARD_T5S3_PRO", "BOARD_XTEINK_X4_PRO"):
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-variable','-D'+board,'-I'+str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True,timeout=30)
        cases = ['home','reader','desk','desk-reader','missing','rejected','invalid','explicit','custom','recovery','display','retained']
        if board == "BOARD_XTEINK_X4_PRO":
            cases.append("notready")
        for scenario in cases:
            subprocess.run([str(binary),scenario],check=True,timeout=5)
