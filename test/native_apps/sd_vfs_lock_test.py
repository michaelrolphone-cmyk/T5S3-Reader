#!/usr/bin/env python3
"""Compile the production loader VFS; fail lock acquisition before any file access."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    'esp_err.h': '#pragma once\nusing esp_err_t=int; constexpr int ESP_OK=0, ESP_ERR_INVALID_STATE=1, ESP_ERR_NO_MEM=2;\n',
    'freertos/FreeRTOS.h': '''#pragma once
#include <cstdint>
using TickType_t=unsigned; struct StaticSemaphore_t {};
constexpr int pdTRUE=1; constexpr unsigned portMAX_DELAY=~0u;
#define pdMS_TO_TICKS(ms) (ms)
''',
    'freertos/semphr.h': '''#pragma once
#include "FreeRTOS.h"
#include <cassert>
using SemaphoreHandle_t=StaticSemaphore_t*;
namespace Probe { inline bool deny=false,held=false; inline unsigned takes=0,gives=0,ticks=0; }
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* s) {return s;}
inline int xSemaphoreTake(SemaphoreHandle_t s,unsigned ticks) {
 assert(s&&!Probe::held); ++Probe::takes; Probe::ticks=ticks;
 if(Probe::deny) {return 0;}
 Probe::held=true; return pdTRUE;
}
inline void xSemaphoreGive(SemaphoreHandle_t) {assert(Probe::held);Probe::held=false;++Probe::gives;}
''',
    'esp_vfs.h': '''#pragma once
#include "esp_err.h"
#include <sys/types.h>
#include <sys/stat.h>
constexpr int ESP_VFS_FLAG_DEFAULT=0;
struct esp_vfs_t {int flags; int(*open)(const char*,int,int); ssize_t(*read)(int,void*,size_t);
 off_t(*lseek)(int,off_t,int); int(*close)(int); int(*fstat)(int,struct stat*);};
inline esp_vfs_t captured{};
inline esp_err_t esp_vfs_register(const char*,const esp_vfs_t* v,void*) {captured=*v;return ESP_OK;}
''',
    'HalStorage.h': '''#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
namespace Probe {inline unsigned fileCalls=0,closed=0;inline bool closeFail=false;}
class HalFile {
 bool open_=false; size_t position_=0;
 public:
 HalFile()=default; explicit HalFile(bool opened):open_(opened){}
 bool isOpen() const {++Probe::fileCalls;return open_;}
 explicit operator bool() const {return isOpen();}
 bool isDirectory() const {++Probe::fileCalls;return false;}
 uint64_t fileSize64() const {++Probe::fileCalls;return 64;}
 size_t position() const {++Probe::fileCalls;return position_;}
 bool seek64(uint64_t p) {++Probe::fileCalls;position_=p;return true;}
 int read(void* p,size_t n) {++Probe::fileCalls;std::memset(p,0x42,n);position_+=n;return n;}
 bool close() {++Probe::fileCalls;if(Probe::closeFail)return false;open_=false;++Probe::closed;return true;}
};
struct Store {bool ready() const {return true;} HalFile open(const char*,int) {++Probe::fileCalls;return HalFile(true);}};
inline Store Storage;
'''
}
TEST = r'''
#include <cassert>
#include <cstdio>
#include <cerrno>
#include "freertos/semphr.h"
#include "HalStorage.h"
#include "esp_vfs.h"
#include "NativeAppLauncher.h"
int main() {
 assert(native_app_register_sd_vfs()==ESP_OK);
 const int fd=captured.open("/Apps/app.elf",O_RDONLY,0);assert(fd==0);
 char byte=0;struct stat st{};
 Probe::deny=true;const auto calls=Probe::fileCalls,gives=Probe::gives,closed=Probe::closed;
 assert(captured.open("/Apps/other.elf",O_RDONLY,0)==-1&&errno==ETIMEDOUT);
 assert(captured.read(fd,&byte,1)==-1&&errno==ETIMEDOUT&&byte==0);
 assert(captured.lseek(fd,0,SEEK_SET)==-1&&errno==ETIMEDOUT);
 assert(captured.fstat(fd,&st)==-1&&errno==ETIMEDOUT);
 assert(captured.close(fd)==-1&&errno==ETIMEDOUT);
 assert(Probe::fileCalls==calls&&Probe::gives==gives&&Probe::closed==closed);
 assert(Probe::ticks>0&&Probe::ticks==1000&&Probe::ticks!=portMAX_DELAY);
 Probe::deny=false;
 assert(captured.read(fd,&byte,1)==1&&byte==0x42);
 assert(captured.lseek(fd,5,SEEK_SET)==5);
 assert(captured.fstat(fd,&st)==0&&st.st_size==64);
 assert(captured.open("/Apps/other.elf",O_RDONLY,0)==1); // timed-out close retained slot 0
 assert(captured.open("/Apps/third.elf",O_RDONLY,0)==2);
 assert(captured.open("/Apps/fourth.elf",O_RDONLY,0)==3);
 assert(captured.open("/Apps/fifth.elf",O_RDONLY,0)==-1&&errno==EMFILE);
 Probe::closeFail=true;assert(captured.close(fd)==-1&&errno==EIO);
 assert(captured.open("/Apps/fifth.elf",O_RDONLY,0)==-1&&errno==EMFILE);
 Probe::closeFail=false;assert(captured.close(fd)==0);
 assert(captured.open("/Apps/new.elf",O_RDONLY,0)==fd);
 assert(captured.open("/Apps/write.elf",O_WRONLY,0)==-1&&errno==EROFS);
 for(int i=0;i<4;++i)assert(captured.close(i)==0);
 assert(captured.read(0,&byte,1)==-1&&errno==EBADF);
 assert(!Probe::held);
 puts("Production SD VFS: bounded lock refusal, no file access/unlock on timeout, retained close slots and normal reads PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='u1-vfs-lock-') as directory:
    path=Path(directory)
    for name, text in HEADERS.items():
        target=path/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(text)
    (path/'test.cpp').write_text(TEST)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I'+str(path),'-I'+str(ROOT/'lib/NativeApps/include'),
                    str(ROOT/'lib/NativeApps/src/SdVfs.cpp'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True,env=os.environ.copy())
