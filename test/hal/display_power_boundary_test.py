#!/usr/bin/env python3
"""Exercise the actual Reader sleep boundary when external panel power refuses."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'lib/hal/HalDisplay.cpp').read_text()
method=source[source.index('bool HalDisplay::deepSleep()'):source.index('void HalDisplay::setIdlePowerSaving')]
fixture=r'''
#include <cassert>
#include <cstdio>
static bool storageOkay=true,powerOkay=true;
static unsigned waits,powerCalls,sleeps,deinits,cancels;
namespace Board {
 bool prepareForSleep(){return storageOkay;}
 void deinitForSleep(){++deinits;}
}
bool halStorageCancelSleep(){++cancels;return true;}
struct Panel {
 void waitDisplay(){++waits;}
 bool setPowerChecked(bool on){assert(!on);++powerCalls;return powerOkay;}
 void sleep(){++sleeps;}
} panel;
struct HalDisplay {Panel*gfx=&panel;bool deepSleep();};
'''
main=r'''
int main(){
 HalDisplay display;
 storageOkay=false;assert(!display.deepSleep());assert(!powerCalls&&!deinits&&!cancels);
 storageOkay=true;powerOkay=false;assert(!display.deepSleep());
 assert(powerCalls==1&&cancels==1&&!sleeps&&!deinits);
 powerOkay=true;assert(display.deepSleep());assert(powerCalls==2&&sleeps==1&&deinits==1&&cancels==1);
 puts("Reader sleep refuses failed power release and thaws untouched SD; retry succeeds PASS");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(fixture+method+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
