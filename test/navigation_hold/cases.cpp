using Button=MappedInputManager::Button;
void tick(uint32_t buttons,uint32_t pressed=0,uint32_t released=0,uint32_t elapsed=20){
 scripted={buttons,pressed,released};fakeTime+=elapsed;nativeNavigationTick();
}
void gesture(uint32_t button,uint32_t duration){
 tick(button,button);tick(button,0,0,duration);tick(0,0,button);
}
int main(){
 TestActivity a;
 tick(0);
 for(uint32_t button:{uint32_t(RISC_NAV_LEFT),uint32_t(RISC_NAV_RIGHT),uint32_t(RISC_NAV_CONFIRM),uint32_t(RISC_NAV_BACK),uint32_t(RISC_NAV_HOME)}){
  for(unsigned repeat=0;repeat<3;++repeat){
   tick(button,button);assert(nativeNavigationHeldMs()==0);
   tick(button,0,0,1000);assert(a.mappedInput.getHeldTime()==1000);
   tick(0,0,button);assert(nativeNavigationHeldMs()==1020);
   assert(a.mappedInput.getHeldTime()==1020);
   fakeTime+=5;assert(nativeNavigationHeldMs()==1020); // fixed event duration
   nativeNavigationTick();assert(!nativeNavigationFrame().released&&nativeNavigationHeldMs()==0);
  }
 }
 // Actual reader decisions: short/threshold/long, both directions and settings.
 for(uint32_t button:{uint32_t(RISC_NAV_LEFT),uint32_t(RISC_NAV_RIGHT)}){
  bool forward=button==RISC_NAV_RIGHT;
  for(unsigned setting:{unsigned(SETTINGS.CHAPTER_SKIP),unsigned(SETTINGS.ORIENTATION_CHANGE)}){
   SETTINGS.longPressButtonBehavior=setting;
   for(uint32_t held:{uint32_t(100),uint32_t(680),uint32_t(681),uint32_t(1000)}){
    a.currentSpineIndex=5;a.currentPage=50;a.turns=0;SETTINGS.orientation=0;
    gesture(button,held);
    a.epubPageLoop();a.xtcLoop();
    bool longPress=held+20>700;
    assert(a.currentSpineIndex==5+(longPress&&setting==SETTINGS.CHAPTER_SKIP?(forward?1:-1):0));
    assert(SETTINGS.orientation==(longPress&&setting==SETTINGS.ORIENTATION_CHANGE?(forward?3:1):0));
    assert(a.turns==unsigned(!longPress));
    assert(a.currentPage==unsigned(50+(forward?1:-1)*(longPress&&setting==SETTINGS.CHAPTER_SKIP?10:1)));
   }
  }
 }
 SETTINGS.longPressButtonBehavior=SETTINGS.OFF;
 tick(RISC_NAV_RIGHT,RISC_NAV_RIGHT);a.currentPage=50;a.xtcLoop();assert(a.currentPage==51);
 tick(0,0,RISC_NAV_RIGHT,1000);a.xtcLoop();assert(a.currentPage==51);
 SETTINGS.longPressButtonBehavior=SETTINGS.CHAPTER_SKIP;
 // Ordinary semantic Confirm and Back still dispatch; a long Back release does
 // not become a short Back/Home request after its hold already opened the browser.
 gesture(RISC_NAV_CONFIRM,100);a.xtcLoop();assert(a.menus==1);
 gesture(RISC_NAV_BACK,100);a.xtcLoop();assert(a.homes==1);
 tick(RISC_NAV_BACK,RISC_NAV_BACK);tick(RISC_NAV_BACK,0,0,1100);a.xtcLoop();assert(a.browsers==1);
 tick(0,0,RISC_NAV_BACK);a.xtcLoop();assert(a.homes==1);
 // Chords, substitution, unrelated release and same-frame taps cannot borrow
 // a preceding button's duration. Remaining held key starts its own interval.
 tick(RISC_NAV_LEFT,RISC_NAV_LEFT);tick(RISC_NAV_LEFT,0,0,2000);
 tick(RISC_NAV_RIGHT,RISC_NAV_RIGHT,RISC_NAV_LEFT);assert(a.mappedInput.getHeldTime()==0);
 tick(0,0,RISC_NAV_RIGHT);assert(a.mappedInput.getHeldTime()==20);
 tick(RISC_NAV_LEFT|RISC_NAV_RIGHT,RISC_NAV_LEFT|RISC_NAV_RIGHT);
 tick(RISC_NAV_LEFT|RISC_NAV_RIGHT,0,0,2000);assert(a.mappedInput.getHeldTime()==0);
 tick(0,0,RISC_NAV_LEFT|RISC_NAV_RIGHT);assert(a.mappedInput.getHeldTime()==0);
 tick(RISC_NAV_LEFT,RISC_NAV_LEFT);tick(0,0,RISC_NAV_RIGHT,2000);assert(a.mappedInput.getHeldTime()==0);
 tick(0,RISC_NAV_LEFT,RISC_NAV_LEFT);assert(a.mappedInput.getHeldTime()==0);
 tick(RISC_NAV_LEFT,RISC_NAV_LEFT);tick(RISC_NAV_LEFT|RISC_NAV_RIGHT,RISC_NAV_RIGHT,0,2000);
 tick(RISC_NAV_RIGHT,0,RISC_NAV_LEFT);assert(a.mappedInput.getHeldTime()==0);
 tick(0,0,RISC_NAV_RIGHT);assert(a.mappedInput.getHeldTime()==20);
 tick(RISC_NAV_LEFT|RISC_NAV_RIGHT,RISC_NAV_LEFT|RISC_NAV_RIGHT);
 tick(RISC_NAV_RIGHT,0,RISC_NAV_LEFT,2000);assert(a.mappedInput.getHeldTime()==0);
 tick(RISC_NAV_RIGHT,0,0,1000);assert(a.mappedInput.getHeldTime()==1000);
 tick(0,0,RISC_NAV_RIGHT);assert(a.mappedInput.getHeldTime()==1020);
 // GPIO-only duration and injected taps retain their existing semantics. Mixed
 // local/provider gestures have no uniquely attributable scalar hold duration.
 tick(0);a.gpio.duration=1234;assert(a.mappedInput.getHeldTime()==1234);
 gesture(RISC_NAV_LEFT,1000);
 for(unsigned state=0;state<3;++state){
  a.gpio.pressed=state==0;a.gpio.released=state==1;a.gpio.held=state==2;
  assert(a.mappedInput.getHeldTime()==0);
 }
 a.gpio.pressed=a.gpio.released=a.gpio.held=0;
 a.mappedInput.injectButtonTap(Button::PageForward);assert(a.mappedInput.getHeldTime()==0);
 a.mappedInput.clearInjectedButtonTap();a.gpio.duration=0;
 // Tilt and render barriers do not turn a retained button duration into a skip.
 gesture(RISC_NAV_RIGHT,1000);SETTINGS.tiltPageTurn=true;halTiltSensor.next=true;
 a.currentPage=50;a.xtcLoop();assert(a.currentPage==51);
 halTiltSensor.next=false;SETTINGS.tiltPageTurn=false;ReaderUtils::blocked=true;
 a.xtcLoop();assert(a.currentPage==51);ReaderUtils::blocked=false;
 // Focus, reset, disable/suspend, failures and replacement clear the event.
 for(unsigned boundary=0;boundary<7;++boundary){
  gesture(RISC_NAV_LEFT,1000);assert(nativeNavigationHeldMs()>700);
  switch(boundary){
   case 0:nativeNavigationBoundary();break;
   case 1:assert(nativeNavigationClaim(1,"test",1));nativeNavigationRelease(1);break;
   case 2:nativeNavigationConfigure(false);nativeNavigationConfigure(true);break;
   case 3:assert(nativeNavigationSuspend());selected=&second;nativeNavigationResume();break;
   case 4:pollOkay=false;tick(0);pollOkay=true;break;
   case 5:resetOkay=false;nativeNavigationBoundary();resetOkay=true;nativeNavigationResume();break;
   case 6:releaseOkay=false;assert(!nativeNavigationSuspend());releaseOkay=true;nativeNavigationResume();break;
  }
  assert(nativeNavigationHeldMs()==0);
  tick(0,0,RISC_NAV_LEFT);assert(nativeNavigationHeldMs()==0);
 }
 // Reset/failed focus during a hold also prevents its later release from
 // resurrecting the old interval, rather than only clearing a completed event.
 tick(RISC_NAV_LEFT,RISC_NAV_LEFT);tick(RISC_NAV_LEFT,0,0,1000);
 nativeNavigationBoundary();tick(0,0,RISC_NAV_LEFT);assert(nativeNavigationHeldMs()==0);
 tick(RISC_NAV_LEFT,RISC_NAV_LEFT);tick(RISC_NAV_LEFT,0,0,1000);
 foregroundOkay=false;assert(!nativeNavigationClaim(9,"failed",1));
 tick(0,0,RISC_NAV_LEFT);assert(nativeNavigationHeldMs()==0);
 foregroundOkay=true;nativeNavigationResume();
 // Clock subtraction has the same uint32 wrap behavior on host and target.
 nativeNavigationBoundary();fakeTime=UINT32_MAX-500;tick(RISC_NAV_RIGHT,RISC_NAV_RIGHT);
 tick(RISC_NAV_RIGHT,0,0,1000);assert(nativeNavigationHeldMs()==1000);
 tick(0,0,RISC_NAV_RIGHT);assert(nativeNavigationHeldMs()==1020);
 assert(nativeNavigationSuspend());
 puts("Production navigation release duration + EPUB/XTC long press, semantic input, ambiguity, lifecycle and rollover: PASS");
}
