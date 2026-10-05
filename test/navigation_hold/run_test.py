#!/usr/bin/env python3
"""Production navigation consumer, mapped duration and reader release decisions."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get('NAVIGATION_HOLD_SOURCE_ROOT', ROOT))
def method(text, signature):
    a = text.index(signature)
    b = text.index('{', a) + 1
    depth = 1
    while depth:
        depth += (text[b] == '{') - (text[b] == '}')
        b += 1
    return text[a:b]
mapped = (SOURCE/'src/MappedInputManager.cpp').read_text()
reader = (SOURCE/'src/activities/reader/ReaderUtils.h').read_text()
epub = (SOURCE/'src/activities/reader/EpubReaderActivity.cpp').read_text()
xtc = (SOURCE/'src/activities/reader/XtcReaderActivity.cpp').read_text()
stub = r'''
#pragma once
#include <cstdint>
#include <cstddef>
struct HalGPIO {
 enum {BTN_BACK=0,BTN_CONFIRM=1,BTN_LEFT=2,BTN_RIGHT=3,BTN_UP=4,BTN_DOWN=5,BTN_POWER=6,BTN_PCA=7};
 uint8_t pressed=0,released=0,held=0;
 unsigned long duration=0;
 bool wasPressed(uint8_t b)const{return pressed&(1u<<b);}
 bool wasReleased(uint8_t b)const{return released&(1u<<b);}
 bool isPressed(uint8_t b)const{return held&(1u<<b);}
 bool wasAnyPressed()const{return pressed;}
 bool wasAnyReleased()const{return released;}
 unsigned long getHeldTime()const{return duration;}
};
'''
prefix = r'''
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>
#include <HalStorage.h>
#include "MappedInputManager.h"
#include "native/NativeNavigationInput.cpp"
uint32_t fakeTime=1000;
bool storageReady=true,closeOk=true;
std::map<std::string,std::shared_ptr<TestFile>> files;
HalStorage Storage;
risc_input_navigation_frame_v1 scripted{};
bool pollOkay=true,resetOkay=true,foregroundOkay=true,releaseOkay=true;
static bool poll(void*,risc_input_navigation_frame_v1*out){*out=scripted;return pollOkay;}
static bool reset(void*){return resetOkay;}
static bool foreground(void*,const risc_input_foreground_v1*,size_t){return foregroundOkay;}
const risc_input_navigation_api_v1 first{1,sizeof(first),nullptr,poll,foreground,reset};
const risc_input_navigation_api_v1 second{1,sizeof(second),nullptr,poll,foreground,reset};
const risc_input_navigation_api_v1*selected=&first;
namespace RuntimeInstalledProviders {
 bool acquireCapability(const char*,uint32_t,Lease*out){*out={{1,1},selected};return true;}
 bool release(Lease*out){if(!releaseOkay)return false;*out={};return true;}
 bool hasLiveGrants(){return false;}
 const char*lastError(){return "fixture";}
}
bool drainPlatformProvidersForSleep(){return true;}
struct CrossPointSettings {
 enum SIDE_BUTTON_LAYOUT {PREV_NEXT,NEXT_PREV};
 enum {OFF,CHAPTER_SKIP,ORIENTATION_CHANGE,ORIENTATION_COUNT=4};
};
struct:CrossPointSettings {
 uint8_t sideButtonLayout=0,frontButtonBack=0,frontButtonConfirm=1,frontButtonLeft=2,frontButtonRight=3;
 uint8_t longPressButtonBehavior=CHAPTER_SKIP,orientation=0;
 bool tiltPageTurn=false;
}SETTINGS;
struct {bool next=false,prev=false;bool wasTiltedForward(){return next;}bool wasTiltedBack(){return prev;}}halTiltSensor;
using ButtonIndex=uint8_t;
struct SideLayoutMap{ButtonIndex pageBack,pageForward;};
constexpr SideLayoutMap kSideLayouts[]={{HalGPIO::BTN_UP,HalGPIO::BTN_DOWN},{HalGPIO::BTN_DOWN,HalGPIO::BTN_UP}};
'''
signatures = ['uint32_t navigationBit(', 'bool MappedInputManager::mapButton(',
              'bool MappedInputManager::wasPressed(', 'bool MappedInputManager::wasReleased(',
              'bool MappedInputManager::isPressed(', 'unsigned long MappedInputManager::getHeldTime(',
              'void MappedInputManager::injectButtonTap(', 'void MappedInputManager::clearInjectedButtonTap(']
production = '\n'.join(method(mapped, s) for s in signatures)
production += '\nnamespace ReaderUtils {\nstruct PageTurnResult{bool prev,next,fromTilt;};\n'
production += method(reader, 'inline PageTurnResult detectPageTurn(')
production += r'''
 bool blocked=false;
 bool isPageTurnInputBlocked(){return blocked;}
 void requestPageTurnEffect(int,bool){}
}
struct RenderLock {template<class T> explicit RenderLock(T&){} };
struct Book {unsigned getPageCount(){return 100;}int getSpineItemsCount(){return 20;}};
struct Section {void reset(){} explicit operator bool()const{return true;}};
struct TestActivity {
 HalGPIO gpio;
 MappedInputManager mappedInput{gpio};
 Book book;Book*xtc=&book;Book*epub=&book;Section section;
 int currentSpineIndex=5,nextPageNumber=2,pendingPageJump=0,renderer=0;
 uint32_t currentPage=50;
 unsigned updates=0,homes=0,menus=0,browsers=0,turns=0;
 bool lastForward=false;
 struct Manager {unsigned*count;void goToInstalledApp(const char*,const char*){++*count;}}activityManager{&browsers};
 void onGoHome(){++homes;}void requestUpdate(){++updates;}
 void openChapterSelection(){++menus;}void maybeAutoRemoveFromRecents(){}
 void applyOrientation(uint8_t o){SETTINGS.orientation=o;}
 void pageTurn(bool forward){++turns;lastForward=forward;}
 void epubPageLoop();void xtcLoop();
};
'''
# Execute the complete existing EPUB page-turn block and full XTC loop, not a
# reimplementation of their duration threshold, chapter/orientation or page logic.
epub_loop = method(epub, 'void EpubReaderActivity::loop()')
production += '\nvoid TestActivity::epubPageLoop(){\n' + epub_loop[epub_loop.index('  auto [prevTriggered'):]
production += '\n' + method(xtc, 'void XtcReaderActivity::loop()').replace('XtcReaderActivity::loop()', 'TestActivity::xtcLoop()')
for source, name in [(epub, 'skipChapterMs'), (xtc, 'skipPageMs'), (xtc, 'goHomeMs')]:
    line = next(line for line in source.splitlines() if line.startswith('constexpr unsigned long '+name+' ='))
    prefix += '\n' + line
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp/'HalGPIO.h').write_text(stub)
    (temp/'test.cpp').write_text(prefix + '\n' + production + '\n' + (ROOT/'test/navigation_hold/cases.cpp').read_text())
    for sanitize in [False, True]:
        command = ['c++','-std=c++17','-Wall','-Wextra','-Werror','-g',
                   '-I'+str(temp),'-I'+str(SOURCE/'src'),'-I'+str(SOURCE/'sdk/driver'),
                   '-I'+str(SOURCE/'test/streams/stubs'),'-I'+str(SOURCE/'test/drivers/stubs')]
        if sanitize: command += ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
        command += [str(temp/'test.cpp'),'-o',str(temp/'test')]
        subprocess.run(command,check=True)
        subprocess.run([str(temp/'test')],check=True)
