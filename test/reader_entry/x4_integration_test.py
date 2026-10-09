#!/usr/bin/env python3
"""Execute X4's real startup gates and the main-loop deferred idle-sleep latch."""
from pathlib import Path
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
def run(source):
    with tempfile.TemporaryDirectory(prefix='reader-x4-entry-') as directory:
        cpp = Path(directory)/'test.cpp'
        binary = Path(directory)/'test'
        cpp.write_text(source)
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        '-I'+str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True,timeout=45)
        subprocess.run([str(binary)],check=True,timeout=10,env=os.environ)
boot = (ROOT/'src/platform/X4DiagnosticBoot.cpp').read_text()
run(r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
template<class... T> void logInfo(T...){ }
#define LOG_INF(...) logInfo(__VA_ARGS__)
unsigned nowMs=0,scheduled=0,presented=0,polls=0,navigation=0;
bool startup_prepared=false,ready=false,showing_home=false,logSerial=false;
unsigned long millis(){return nowMs;}
void delay(unsigned ms){nowMs+=ms;}
struct Surface{bool complete=false;bool lastPresentSucceeded(){return complete;}}surface;
Surface*provider_surface=&surface;
namespace X4BootDiagnostics {
 enum class Stage{HomePresent,Ready};
 void mark(Stage stage){if(stage==Stage::HomePresent)++scheduled;else ++presented;}
 void poll(bool){++polls;}
}
struct risc_input_navigation_frame_v1{uint32_t pressed=0;};
enum{RISC_NAV_LEFT=1,RISC_NAV_RIGHT=2,RISC_NAV_CONFIRM=4,RISC_NAV_BACK=8};
risc_input_navigation_frame_v1 nativeNavigationFrame(){assert(ready);++navigation;return {};}
""" + '\n'.join(method(boot,s) for s in (
    'bool x4ReaderStartupReady()', 'void x4ReaderActivityScheduled()', 'bool x4DiagnosticLoop()')) + r"""
int main(){
 // Failed boot may report/reconnect but never becomes eligible for an ELF.
 assert(!x4ReaderStartupReady());
 for(unsigned i=0;i<12;++i)assert(!x4DiagnosticLoop());
 assert(!ready&&!navigation&&polls);
 // Boot prerequisites complete independently of a Home frame. This allows
 // the default ELF's first pump to create Home instead of deadlocking.
 startup_prepared=true;
 assert(x4ReaderStartupReady()&&!ready&&!showing_home);
 // A leftover splash completion cannot qualify the Reader surface.
 surface.complete=true;
 assert(!x4DiagnosticLoop()&&!ready&&!navigation);
 surface.complete=false;
 x4ReaderActivityScheduled();
 assert(scheduled==1&&showing_home&&!ready);
 // Failed/pending first frame keeps ordinary input gated on repeated loops.
 for(unsigned i=0;i<12;++i)assert(!x4DiagnosticLoop());
 assert(!navigation&&!presented&&x4ReaderStartupReady());
 surface.complete=true;
 assert(x4DiagnosticLoop()&&ready&&presented==1&&navigation==1);
 assert(x4DiagnosticLoop()&&presented==1&&navigation==2);
 puts("Production X4 entry gates: failed boot, prior splash, deferred Home, failed frame, successful input admission PASS");
}
""")
main=(ROOT/'src/main.cpp').read_text()
idle_block=method(main,'  if (idleSleep.pending()) {')
sleep_callback=method(main,'static void runReaderSleep(')
run(r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "runtime/power/IdleSleepDeadline.h"
#include "native/NativeReaderEntryState.h"
#define LOG_DBG(...) ((void)0)
IdleSleepDeadline idleSleep;
unsigned nowMs=100,sleepCalls=0,physicalSleeps=0;
unsigned long millis(){return nowMs;}
namespace NativeReaderEntry{
 State state;
 bool pending(){return state.pending();}
}
void enterDeepSleep(){
 ++sleepCalls;
 if(NativeReaderEntry::state.request(NativeReaderEntry::Action::Sleep))return;
 assert(!NativeReaderEntry::state.mapped()&&idleSleep.pending());
 ++physicalSleeps; // The board may refuse or return from sleep.
}
void enterDeepSleepKeepingScreen(bool){}
void enterPowerOffKeepingScreen(const char*){}
void step(){
""" + idle_block + '\n}\n' + sleep_callback + r"""
int main(){
 assert(!idleSleep.observe(0,100,false,false));
 assert(idleSleep.observe(nowMs,100,false,false));
 assert(NativeReaderEntry::state.begin());
 step();
 assert(sleepCalls==1&&!physicalSleeps&&idleSleep.pending()&&NativeReaderEntry::pending());
 idleSleep.activity(++nowMs); // Reader/child return cannot erase the latch.
 assert(idleSleep.pending());
 NativeReaderEntry::state.end(true);
 const auto pending=NativeReaderEntry::state.take();
 runReaderSleep(pending.action,pending.wakeOnTouch);
 assert(sleepCalls==2&&physicalSleeps==1&&!idleSleep.pending());
 assert(!idleSleep.observe(nowMs+99,100,false,false));
 assert(idleSleep.observe(nowMs+100,100,false,false));
 nowMs+=100;
 step(); // The ordinary missing-ELF fallback consumes only after real sleep.
 assert(sleepCalls==3&&physicalSleeps==2&&!idleSleep.pending());
 puts("Production Reader idle sleep: latch survives mapped handoff, unload precedes sleep, refused-sleep retry and fallback PASS");
}
""")
