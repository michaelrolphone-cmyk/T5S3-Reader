#!/usr/bin/env python3
"""Real button-provider debounce, MappedInputManager and ReaderUtils page policy."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path(os.environ.get("PAGE_BUTTON_SOURCE_ROOT",ROOT))
def method(s,signature):
    a=s.index(signature);b=s.index('{',a)+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
mapped=(SOURCE/'src/MappedInputManager.cpp').read_text()
reader=(SOURCE/'src/activities/reader/ReaderUtils.h').read_text()
main=(ROOT/'src/main.cpp').read_text()
stub=r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <BoardCapabilities.h>
struct HalGPIO{
 enum {BTN_BACK=0,BTN_CONFIRM=1,BTN_LEFT=2,BTN_RIGHT=3,BTN_UP=4,BTN_DOWN=5,BTN_POWER=6};
 uint8_t pressed=0,released=0,held=0;
 bool wasPressed(uint8_t b)const{return pressed&(1u<<b);}
 bool wasReleased(uint8_t b)const{return released&(1u<<b);}
 bool isPressed(uint8_t b)const{return held&(1u<<b);}
 unsigned long getHeldTime()const{return 0;}
};
'''
prefix=r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "MappedInputManager.h"
#include <RiscInputNavigationV1.h>
#include <RiscProviderV2.h>
#include "x4pro_pins.h"
extern "C" {uint32_t x4_fake_pressed_pins=0,x4_fake_input_pins=0;}
bool enabled=true,usable=true,quarantined=false;
const risc_input_navigation_api_v1*api=nullptr;
const risc_input_navigation_api_v1 semantic{1,sizeof(semantic),nullptr,nullptr,nullptr,nullptr};
struct CrossPointSettings{enum SIDE_BUTTON_LAYOUT{PREV_NEXT,NEXT_PREV};enum{OFF=0};};
struct:CrossPointSettings{
 uint8_t sideButtonLayout=0,frontButtonBack=0,frontButtonConfirm=1,frontButtonLeft=2,frontButtonRight=3;
 uint8_t flipUi=0,longPressButtonBehavior=0;
 bool tiltPageTurn=false;
}SETTINGS;
risc_input_navigation_frame_v1 frame{};
const risc_input_navigation_frame_v1&nativeNavigationFrame(){return frame;}
unsigned long nativeNavigationHeldMs(){return frame.buttons?900:0;}
struct{bool next=false,prev=false;bool wasTiltedForward(){return next;}bool wasTiltedBack(){return prev;}}halTiltSensor;
using ButtonIndex=uint8_t;
struct SideLayoutMap{ButtonIndex pageBack,pageForward;};
constexpr SideLayoutMap kSideLayouts[]={{HalGPIO::BTN_UP,HalGPIO::BTN_DOWN},{HalGPIO::BTN_DOWN,HalGPIO::BTN_UP}};
struct PageTurnResult{bool prev,next,fromTilt;};
'''
signatures=['uint32_t navigationBit(', 'bool MappedInputManager::mapButton(',
            'bool MappedInputManager::wasPressed(', 'bool MappedInputManager::wasReleased(',
            'bool MappedInputManager::isPressed(', 'unsigned long MappedInputManager::getHeldTime(',
            'void MappedInputManager::injectButtonTap(', 'void MappedInputManager::clearInjectedButtonTap(']
if 'bool MappedInputManager::wasPageTurnRequested(' in mapped:
    signatures+=['bool MappedInputManager::wasPageTurnRequested(']
navigation_source=(ROOT/'src/native/NativeNavigationInput.cpp').read_text()
production=method(navigation_source,'bool nativeNavigationHasPhysicalPagePair()')+'\n'+'\n'.join(method(mapped,s) for s in signatures)+'\n'+method(reader,'inline PageTurnResult detectPageTurn(')
a=main.index('  const bool flipUi = SETTINGS.flipUi != 0;',main.index('// Reader page-turn direction'))
b=main.index('  // Non-reader navigation:',a)
production+='\nMappedInputManager::Button t5PhysicalPage(bool second){\n'+main[a:b]+'\nreturn second?pcaPageButton:bootPageButton;}\n'
tests=r'''
using Button=MappedInputManager::Button;
const risc_driver_v2*driver;
const risc_input_navigation_api_v1*navigation;
void tick(){assert(navigation->poll(navigation->context,&frame));}
void assertTurn(const MappedInputManager&input,bool prev,bool next){
 const auto result=detectPageTurn(input);assert(result.prev==prev&&result.next==next);
 // Querying the same frame is idempotent, never consumes/remaps the event.
 const auto repeat=detectPageTurn(input);assert(repeat.prev==prev&&repeat.next==next);
}
void neutral(){x4_fake_pressed_pins=0;for(unsigned i=0;i<4;++i)tick();}
int main(){
 driver=t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);assert(driver&&driver->start(nullptr,0));
 navigation=static_cast<const risc_input_navigation_api_v1*>(driver->capability);
 api=navigation;assert(nativeNavigationHasPhysicalPagePair());
 // Prefix-only, short, mistagged and unsupported-version providers are semantic.
 assert(!risc_input_navigation_has_physical_page_pair(nullptr));
 assert(!risc_input_navigation_has_physical_page_pair(&semantic));
 auto traits=*reinterpret_cast<const risc_input_navigation_traits_v1*>(navigation);
 for(unsigned size=0;size<sizeof(traits);++size){
  traits.base.struct_size=size;assert(!risc_input_navigation_has_physical_page_pair(&traits.base));
 }
 traits=*reinterpret_cast<const risc_input_navigation_traits_v1*>(navigation);
 traits.traits_tag=0;assert(!risc_input_navigation_has_physical_page_pair(&traits.base));
 traits.traits_tag=RISC_INPUT_NAVIGATION_TRAITS_TAG;traits.traits_version=2;
 assert(!risc_input_navigation_has_physical_page_pair(&traits.base));
 traits.traits_version=1;traits.base.api_version=2;assert(!risc_input_navigation_has_physical_page_pair(&traits.base));
 traits.base.api_version=1;traits.traits=2;assert(!risc_input_navigation_has_physical_page_pair(&traits.base));
 traits.traits=3;assert(risc_input_navigation_has_physical_page_pair(&traits.base));
 enabled=false;assert(!nativeNavigationHasPhysicalPagePair());enabled=true;
 usable=false;assert(!nativeNavigationHasPhysicalPagePair());usable=true;
 quarantined=true;assert(!nativeNavigationHasPhysicalPagePair());quarantined=false;
 HalGPIO gpio;MappedInputManager input(gpio);
 for(unsigned release=0;release<2;++release)for(unsigned layout=0;layout<2;++layout)
 for(unsigned flip=0;flip<2;++flip)for(bool second:{false,true}){
  api=navigation;SETTINGS.longPressButtonBehavior=release;SETTINGS.sideButtonLayout=layout;SETTINGS.flipUi=flip;
  neutral();assert(navigation->reset(nullptr));
  const uint32_t bit=second?RISC_NAV_RIGHT:RISC_NAV_LEFT;
  const uint32_t pin=second?X4PRO_PIN_BTN_RIGHT:X4PRO_PIN_BTN_LEFT;
  const bool forward=second!=(layout!=flip);
  for(unsigned repetition=0;repetition<3;++repetition){
   x4_fake_pressed_pins=1u<<pin;
   for(unsigned i=0;i<3;++i){tick();assertTurn(input,false,false);}
   tick();assert(frame.pressed==bit&&frame.buttons==bit);
   assertTurn(input,!release&&!forward,!release&&forward);
   // Ordinary app/menu queries retain the provider's original direction.
   assert(input.wasPressed(second?Button::Right:Button::Left));
   assert(!input.wasPressed(second?Button::Left:Button::Right));
   assert(!input.wasPressed(Button::Back)&&!input.wasPressed(Button::Power));
   for(unsigned i=0;i<10;++i){tick();assertTurn(input,false,false);assert(input.isPressed(second?Button::Right:Button::Left));}
   assert(input.getHeldTime()==900);
   x4_fake_pressed_pins=0;
   for(unsigned i=0;i<3;++i){tick();assertTurn(input,false,false);}
   tick();assert(frame.released==bit&&!frame.buttons);
   assertTurn(input,release&&!forward,release&&forward);
   assert(input.wasReleased(second?Button::Right:Button::Left));
   tick();assertTurn(input,false,false);
  }
  // Focus reset while held must not manufacture a turn on re-entry/release.
  x4_fake_pressed_pins=1u<<pin;assert(navigation->reset(nullptr));
  for(unsigned i=0;i<5;++i){tick();assertTurn(input,false,false);}
  neutral();assertTurn(input,false,false);
  // The T5 input was transformed in main already: do not apply policy twice.
  api=&semantic;frame={};input.injectButtonTap(t5PhysicalPage(second));
  assertTurn(input,!forward,forward);input.clearInjectedButtonTap();
 }
 // Synthetic touch hints and direct semantic navigation retain their meaning.
 for(bool board:{false,true})for(unsigned layout=0;layout<2;++layout)
 for(unsigned flip=0;flip<2;++flip)for(unsigned release=0;release<2;++release){
  api=board?navigation:&semantic;SETTINGS.sideButtonLayout=layout;SETTINGS.flipUi=flip;SETTINGS.longPressButtonBehavior=release;
  for(Button b:{Button::PageBack,Button::Left,Button::PageForward,Button::Right}){
   frame={};input.injectButtonTap(b);
   const bool forward=b==Button::PageForward||b==Button::Right;
   assertTurn(input,!forward,forward);input.clearInjectedButtonTap();
  }
  for(uint32_t bits:{uint32_t(RISC_NAV_PAGE_BACK),uint32_t(RISC_NAV_UP),uint32_t(RISC_NAV_PAGE_FORWARD),uint32_t(RISC_NAV_DOWN)}){
   frame={};frame.pressed=frame.released=bits;
   const bool forward=(bits&(RISC_NAV_PAGE_FORWARD|RISC_NAV_DOWN))!=0;
   assertTurn(input,!forward,forward);
  }
  if(!board){
   for(uint32_t bits:{uint32_t(RISC_NAV_LEFT),uint32_t(RISC_NAV_RIGHT)}){
    frame={};frame.pressed=frame.released=bits;assertTurn(input,bits==RISC_NAV_LEFT,bits==RISC_NAV_RIGHT);
   }
  }
  // Ordinary Back/Confirm/Home binding and raw LEFT/RIGHT never change.
  frame={};frame.pressed=frame.released=frame.buttons=RISC_NAV_BACK|RISC_NAV_CONFIRM|RISC_NAV_HOME|RISC_NAV_LEFT|RISC_NAV_RIGHT;
  const auto before=frame;
  assert(input.wasPressed(Button::Back)&&input.wasReleased(Button::Confirm));
  assert(input.isPressed(Button::Left)&&input.isPressed(Button::Right)&&!input.isPressed(Button::Power));
  assert(frame.buttons==before.buttons&&frame.pressed==before.pressed&&frame.released==before.released);
 }
 // Real X4 power-button provider mapping stays Confirm, not page or power-off.
 api=navigation;neutral();assert(navigation->reset(nullptr));x4_fake_pressed_pins=1u<<X4PRO_PIN_BTN_POWER;
 for(unsigned i=0;i<4;++i)tick();
 assert(frame.pressed==RISC_NAV_CONFIRM&&input.wasPressed(Button::Confirm)&&!input.isPressed(Button::Power));
 assertTurn(input,false,false);neutral();
 // Preserve GPIO side/front semantics, injected taps and tilt union on generic boards.
 api=&semantic;frame={};SETTINGS.flipUi=1;
 for(unsigned release=0;release<2;++release)for(unsigned layout=0;layout<2;++layout){
  SETTINGS.longPressButtonBehavior=release;SETTINGS.sideButtonLayout=layout;
  gpio.pressed=gpio.released=1u<<HalGPIO::BTN_UP;assertTurn(input,layout==0,layout!=0);
  gpio.pressed=gpio.released=1u<<HalGPIO::BTN_DOWN;assertTurn(input,layout!=0,layout==0);
  gpio.pressed=gpio.released=1u<<HalGPIO::BTN_LEFT;assertTurn(input,true,false);
  gpio.pressed=gpio.released=1u<<HalGPIO::BTN_RIGHT;assertTurn(input,false,true);
 }
 gpio.pressed=gpio.released=0;SETTINGS.tiltPageTurn=true;halTiltSensor.next=true;
 assertTurn(input,false,true);halTiltSensor.next=false;halTiltSensor.prev=true;assertTurn(input,true,false);
 driver->stop();assert(!navigation->poll(nullptr,&frame));
 puts("Production X4 driver + page policy: both buttons, press/release, layout/flip, repeat/held/reset, semantic/touch injection, T5 no-double-swap, ordinary navigation and tilt PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='page-buttons-') as directory:
    tmp=Path(directory);(tmp/'HalGPIO.h').write_text(stub)
    cpp=tmp/'test.cpp';cpp.write_text(prefix+production+tests)
    obj=tmp/'buttons.o';binary=tmp/'test'
    san=[] if os.environ.get("PAGE_BUTTON_SANITIZE","1")=="0" else ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    common=['-Wall','-Wextra','-Werror',*san]
    includes=['-I'+str(tmp),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'sdk/driver'),
              '-I'+str(ROOT/'Drivers/x4pro_board'),'-I'+str(ROOT/'lib/Board'),
              '-I'+str(ROOT/'lib/Board_X4Pro'),'-I'+str(ROOT/'test/native_battery/stubs')]
    subprocess.run(['cc','-std=c11',*common,'-I'+str(ROOT/'test/x4pro_buttons_fake'),
                    *includes,'-c',str(ROOT/'Drivers/x4pro_buttons/driver.c'),'-o',str(obj)],check=True,timeout=45)
    subprocess.run(['c++','-std=c++17',*common,*includes,str(cpp),
                    str(obj),'-o',str(binary)],check=True,timeout=45)
    subprocess.run([str(binary)],check=True,timeout=15,env=os.environ)
