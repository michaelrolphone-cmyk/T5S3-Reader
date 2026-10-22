#!/usr/bin/env python3
"""Exercise production VFS registration/callbacks with explicitly chosen volumes."""
from sd_vfs_lock_test import HEADERS, run_test

headers = dict(HEADERS)
headers['esp_vfs.h'] = ('#include <cassert>\n#include <cstring>\n' + headers['esp_vfs.h']).replace(
    'inline esp_err_t esp_vfs_unregister(const char*) {return ESP_OK;}',
    '''namespace Registration {inline int result=ESP_OK, removed=0; inline char mount[16]{};}
inline esp_err_t esp_vfs_unregister(const char* path) {
 assert(std::strcmp(path,Registration::mount)==0);
 if(Registration::result==ESP_OK)++Registration::removed;
 return Registration::result;
}''').replace(
    'inline esp_err_t esp_vfs_register(const char*,const esp_vfs_t* v,void*) {captured=*v;return ESP_OK;}',
    '''inline esp_err_t esp_vfs_register(const char* path,const esp_vfs_t* v,void*) {
 if(Registration::result==ESP_OK){captured=*v;std::strcpy(Registration::mount,path);}
 return Registration::result;
}''')

test = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <limits>
#include "freertos/semphr.h"
#include "HalStorage.h"
#include "esp_vfs.h"
#include "NativeAppLauncher.h"
#include "NativeAppVolumeVfs.h"
struct Volume {
 risc_storage_volume_api_v1_ext api{};
 uint64_t size=64,position=0;
 unsigned calls=0,opens=0,closes=0;size_t capacity=0;
 bool ready=true,closeFail=false,infoFail=false,seekFail=false,readFail=false,directory=false,oversizedRead=false;
 uint32_t next=1,error=0;unsigned char byte;
 explicit Volume(unsigned char b):byte(b) {
  api.base.api_version=1;api.base.struct_size=sizeof(api);api.base.context=this;
  api.base.ready=[](void* c){auto&v=*static_cast<Volume*>(c);++v.calls;return v.ready;};
  api.file_open=[](void*c,const char*p,uint32_t flags){auto&v=*static_cast<Volume*>(c);++v.calls;
   assert(!std::strcmp(p,"/Apps/demo.elf"));assert(flags==RISC_STORAGE_OPEN_READ);
   if(v.directory)return 0u;
   ++v.opens;v.position=0;return v.next++;};
  api.file_info=[](void*c,uint32_t h,uint64_t*s,uint64_t*p){auto&v=*static_cast<Volume*>(c);++v.calls;assert(h);
   if(v.infoFail)return false;
   *s=v.size;*p=v.position;return true;};
  api.file_seek=[](void*c,uint32_t h,uint64_t p){auto&v=*static_cast<Volume*>(c);++v.calls;assert(h);
   if(v.seekFail||p>v.size)return false;
   v.position=p;return true;};
  api.base.file_read=[](void*c,uint32_t h,void*b,size_t n){auto&v=*static_cast<Volume*>(c);++v.calls;assert(h);
   v.capacity=n;if(v.readFail)return size_t(0);if(v.oversizedRead)return n+1;
   std::memset(b,v.byte,n);v.position+=n;return n;};
  api.handle_error=[](void*c,uint32_t h,bool directory){auto&v=*static_cast<Volume*>(c);++v.calls;assert(h&&!directory);return v.error;};
  api.base.file_close=[](void*c,uint32_t h,bool commit){auto&v=*static_cast<Volume*>(c);++v.calls;assert(h&&commit);
   if(v.closeFail)return false;
   ++v.closes;return true;};
 }
};
int main(int argc,char**) {
 Volume a(0x41),b(0x42);
 const auto* api=&a.api.base;
 assert(native_app_register_volume_vfs(nullptr,api)==ESP_ERR_INVALID_ARG);
 for(const char* p:{"","/","/sd","/sd/sub","/a.b","/abcdefghijklmnop"})
  assert(native_app_register_volume_vfs(p,api)==ESP_ERR_INVALID_ARG);
 a.api.base.api_version=2;assert(native_app_register_volume_vfs("/chosen",api)==ESP_ERR_INVALID_ARG);a.api.base.api_version=1;
 a.api.base.struct_size=sizeof(risc_storage_volume_api_v1);
 assert(native_app_register_volume_vfs("/chosen",api)==ESP_ERR_INVALID_ARG);a.api.base.struct_size=sizeof(a.api);
 auto info=a.api.file_info;a.api.file_info=nullptr;
 assert(native_app_register_volume_vfs("/chosen",api)==ESP_ERR_INVALID_ARG);a.api.file_info=info;
 a.ready=false;assert(native_app_register_volume_vfs("/chosen",api)==ESP_ERR_INVALID_STATE);a.ready=true;
 Registration::result=ESP_ERR_NO_MEM;
 assert(native_app_register_volume_vfs("/chosen",api)==ESP_ERR_NO_MEM);Registration::result=ESP_OK;
 assert(native_app_register_sd_vfs()==ESP_OK);const auto sd=captured;
 assert(native_app_register_volume_vfs("/chosen",api)==ESP_OK);const auto chosen=captured;
 assert(!std::strcmp(Registration::mount,"/chosen"));
 assert(native_app_register_volume_vfs("/chosen",api)==ESP_OK);
 assert(native_app_register_volume_vfs("/other",api)==ESP_ERR_INVALID_STATE);
 assert(native_app_register_volume_vfs("/chosen",&b.api.base)==ESP_ERR_INVALID_STATE);
 if(argc>1) {
  a.size=51;a.closeFail=true;
  assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==-1&&errno==EIO);
  assert(native_app_unregister_volume_vfs(api)==ESP_ERR_INVALID_STATE);
  char c=0;assert(chosen.read(0,&c,1)==-1&&errno==EBADF);
  // The rejected, unreturned slot remains occupied and its provider retained.
  a.size=64;a.closeFail=false;
  assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==1);
  assert(chosen.close(1)==0);
  assert(native_app_unregister_volume_vfs(api)==ESP_ERR_INVALID_STATE);
  puts("Selected volume rejected-open cleanup: retained handle and binding PASS");return 0;
 }
 for(int flags:{O_WRONLY,O_RDWR,O_RDONLY|O_CREAT,O_RDONLY|O_TRUNC,O_RDONLY|O_APPEND})
  assert(chosen.open("/Apps/demo.elf",flags,0)==-1&&errno==EROFS);
 a.directory=true;assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==-1&&errno==ENOENT);a.directory=false;
 for(uint64_t size:{uint64_t(51),uint64_t(8*1024*1024+1)}) {
  a.size=size;assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==-1&&errno==EFBIG);
 }
 a.infoFail=true;assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==-1&&errno==EIO);a.infoFail=false;
 for(uint64_t size:{uint64_t(52),uint64_t(8*1024*1024)}) {
  a.size=size;int fd=chosen.open("/Apps/demo.elf",O_RDONLY,0);assert(fd==0);
  struct stat st{};assert(chosen.fstat(fd,&st)==0&&uint64_t(st.st_size)==size);
  assert(chosen.close(fd)==0);
 }
 a.size=8192;
 int fd=chosen.open("/Apps/demo.elf",O_RDONLY,0);assert(fd==0);
 char bytes[8192]{};struct stat st{};
 Probe::deny=true;const auto calls=a.calls,legacyCalls=Probe::fileCalls;
 assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==-1&&errno==ETIMEDOUT);
 assert(chosen.read(fd,bytes,1)==-1&&errno==ETIMEDOUT);
 assert(chosen.lseek(fd,0,SEEK_SET)==-1&&errno==ETIMEDOUT);
 assert(chosen.fstat(fd,&st)==-1&&errno==ETIMEDOUT);
 assert(chosen.close(fd)==-1&&errno==ETIMEDOUT);
 assert(native_app_unregister_volume_vfs(api)==ESP_ERR_TIMEOUT);
 assert(a.calls==calls&&Probe::fileCalls==legacyCalls);Probe::deny=false;
 assert(chosen.read(fd,bytes,sizeof(bytes))==4096&&a.capacity==4096&&bytes[0]==0x41);
 assert(chosen.read(fd,nullptr,1)==-1&&errno==EINVAL);
 assert(chosen.read(fd,nullptr,0)==0);
 assert(chosen.lseek(fd,1,999)==-1&&errno==EINVAL);
 assert(chosen.lseek(fd,std::numeric_limits<off_t>::max(),SEEK_CUR)==-1&&errno==EINVAL);
 assert(chosen.lseek(fd,std::numeric_limits<off_t>::min(),SEEK_CUR)==-1&&errno==EINVAL);
 assert(chosen.lseek(fd,-1,SEEK_SET)==-1&&errno==EINVAL);
 assert(chosen.lseek(fd,-4,SEEK_END)==8188);
 assert(chosen.lseek(fd,-4,SEEK_CUR)==8184);
 a.seekFail=true;assert(chosen.lseek(fd,0,SEEK_SET)==-1&&errno==EIO);a.seekFail=false;
 a.infoFail=true;assert(chosen.fstat(fd,&st)==-1&&errno==EIO);assert(chosen.lseek(fd,0,SEEK_SET)==-1&&errno==EIO);a.infoFail=false;
 a.error=1;assert(chosen.read(fd,bytes,1)==-1&&errno==EIO);a.error=0;
 a.oversizedRead=true;assert(chosen.read(fd,bytes,1)==-1&&errno==EIO);a.oversizedRead=false;
 a.ready=false;assert(chosen.read(fd,bytes,1)==-1&&errno==EIO);a.ready=true;
 assert(sd.open("/Apps/legacy.elf",O_RDONLY,0)==1);
 assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==2);
 assert(sd.open("/Apps/legacy2.elf",O_RDONLY,0)==3);
 assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==-1&&errno==EMFILE);
 assert(sd.open("/Apps/legacy3.elf",O_RDONLY,0)==-1&&errno==EMFILE);
 a.closeFail=true;assert(chosen.close(fd)==-1&&errno==EIO);
 assert(native_app_unregister_volume_vfs(api)==ESP_ERR_INVALID_STATE);
 assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==-1&&errno==EMFILE);
 a.closeFail=false;assert(chosen.close(fd)==0);assert(chosen.close(2)==0);
 assert(native_app_unregister_volume_vfs(&b.api.base)==ESP_ERR_INVALID_STATE);
 Registration::result=ESP_ERR_NO_MEM;assert(native_app_unregister_volume_vfs(api)==ESP_ERR_NO_MEM);
 assert(native_app_register_volume_vfs("/other",&b.api.base)==ESP_ERR_INVALID_STATE);
 Registration::result=ESP_OK;assert(native_app_unregister_volume_vfs(api)==ESP_OK);
 // Legacy descriptors survive selected-volume removal. A different explicit
 // provider can then be bound; no stale provider or hard-coded SD identity.
 assert(sd.read(1,bytes,1)==1&&bytes[0]==0x42);
 assert(native_app_register_volume_vfs("/second",&b.api.base)==ESP_OK);
 assert(captured.open("/Apps/demo.elf",O_RDONLY,0)==0);
 assert(captured.read(0,bytes,1)==1&&bytes[0]==0x42&&b.opens==1);
 assert(captured.close(0)==0);assert(native_app_unregister_volume_vfs(&b.api.base)==ESP_OK);
 assert(sd.close(1)==0&&sd.close(3)==0);
 assert(a.opens==a.closes&&!Probe::held);
 puts("Selected volume VFS: caller binding, provider isolation, shared four slots, size/flag/seek bounds, errors and checked lifetime PASS");
}
'''
# The fixture deliberately uses range-for initializer lists.
test = '#include <initializer_list>\n' + test
run_test(test, headers)
run_test(test, headers, ('retained-cleanup',))
