#!/usr/bin/env python3
"""Execute the production quarantine loop without actually retaining a process."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/native/NativeAppHost.cpp').read_text()
helper=source[source.index('[[noreturn]] static void retainNativeAppSession()'):source.index('static esp_err_t runNativeAppImpl(')]
prefix=r'''
#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <string>
#define LOG_ERR(...) ((void)0)
static std::jmp_buf returnToTest;
static int delays=0,watchdogs=0;
static std::string lastLaunchError;
void esp_task_wdt_reset(){++watchdogs;}
void delay(unsigned ms){assert(ms==250);if(++delays==3)std::longjmp(returnToTest,1);}
'''
test=r'''
int main(){
 if(!setjmp(returnToTest)){retainNativeAppSession();assert(false);}
 assert(delays==3&&watchdogs==3);
 assert(lastLaunchError.find("Manual reboot required")!=std::string::npos);
 puts("Production retained child session: terminal quarantine, scheduler cooperation, no return/cleanup PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-retained-') as directory:
    temp=Path(directory);cpp=temp/'retained.cpp';binary=temp/'retained'
    cpp.write_text(prefix+helper+test)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True,timeout=30)
    subprocess.run([str(binary)],check=True,timeout=5)
