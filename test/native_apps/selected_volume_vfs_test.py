#!/usr/bin/env python3
"""Exercise production VFS registration/callbacks with explicitly chosen volumes."""
from sd_vfs_lock_test import HEADERS, run_test

headers = dict(HEADERS)
headers['esp_vfs.h'] = headers['esp_vfs.h'].split('using esp_vfs_id_t=int;')[0] + r'''
#include <cassert>
#include <cstring>
#include <cerrno>
using esp_vfs_id_t=int;
namespace Registration {
inline int result=ESP_OK,fdResult=ESP_OK,removed=0;
struct Entry { bool used=false,pathless=false;char path[16]{};esp_vfs_t vfs{}; };
struct Fd {bool used=false;int owner=-1,local=-1;};
inline Entry entries[8];inline Fd fds[8];
inline int add(const char* path,const esp_vfs_t* v,int* id) {
 if(result!=ESP_OK)return result;
 for(int i=0;i<8;++i)if(!entries[i].used){
  entries[i].used=true;entries[i].pathless=!path;entries[i].vfs=*v;
  if(path)std::strcpy(entries[i].path,path);
  if(id)*id=i;
  return ESP_OK;
 }
 return ESP_ERR_NO_MEM;
}
}
inline esp_err_t esp_vfs_register(const char* path,const esp_vfs_t* v,void*) {
 // IDF 4.4.7 deliberately permits duplicate names.
 return Registration::add(path,v,nullptr);
}
inline esp_err_t esp_vfs_register_with_id(const esp_vfs_t* v,void*,esp_vfs_id_t* id) {
 return Registration::add(nullptr,v,id);
}
inline esp_err_t esp_vfs_unregister_with_id(esp_vfs_id_t id) {
 if(Registration::result!=ESP_OK)return Registration::result;
 assert(id>=0&&id<8&&Registration::entries[id].used);
 Registration::entries[id].used=false;++Registration::removed;return ESP_OK;
}
inline esp_err_t esp_vfs_register_fd_with_local_fd(esp_vfs_id_t id,int local,bool permanent,int* fd) {
 assert(id>=0&&id<8&&Registration::entries[id].used&&!permanent);
 if(Registration::fdResult!=ESP_OK)return Registration::fdResult;
 for(int i=0;i<8;++i)if(!Registration::fds[i].used){Registration::fds[i]={true,id,local};*fd=i;return ESP_OK;}
 return ESP_ERR_NO_MEM;
}
namespace Registration {
inline Fd* lookup(int fd) {
 if(fd<0||fd>=8||!fds[fd].used){errno=EBADF;return nullptr;}return &fds[fd];
}
inline int pathOpen(const char* path,int flags,int mode) {
 int best=-1;size_t length=0;
 for(int i=0;i<8;++i)if(entries[i].used&&!entries[i].pathless){
  const size_t n=std::strlen(entries[i].path);
  if(n>length&&!std::strncmp(path,entries[i].path,n)&&(path[n]=='/'||!path[n])){best=i;length=n;}
 }
 if(best<0){errno=ENOENT;return -1;}
 auto& v=entries[best].vfs;
 const int local=v.open(path+length,flags,mode);if(local<0)return -1;
 int global=-1;
 if(esp_vfs_register_fd_with_local_fd(best,local,false,&global)!=ESP_OK){v.close(local);errno=ENOMEM;return -1;}
 return global;
}
inline int callbackClose(int fd) {
 auto*f=lookup(fd);if(!f)return -1;const int rc=entries[f->owner].vfs.close(f->local);
 if(!rc)f->used=false;
 return rc;
}
inline int libcClose(int fd) {
 auto*f=lookup(fd);if(!f)return -1;const int rc=entries[f->owner].vfs.close(f->local);
 // Match esp_vfs_close: even a failed callback loses the global fd mapping.
 f->used=false;return rc;
}
}
'''

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
struct Client {
 const risc_storage_volume_api_v1* api;
 int open(const char* path,int flags,int mode) const {
  if(api)return native_app_open_volume_file(api,path,flags);
  char full[128];std::snprintf(full,sizeof(full),"/sd%s",path);return Registration::pathOpen(full,flags,mode);
 }
 ssize_t read(int fd,void* p,size_t n) const {auto*f=Registration::lookup(fd);return f?Registration::entries[f->owner].vfs.read(f->local,p,n):-1;}
 off_t lseek(int fd,off_t offset,int whence) const {auto*f=Registration::lookup(fd);return f?Registration::entries[f->owner].vfs.lseek(f->local,offset,whence):-1;}
 int fstat(int fd,struct stat* st) const {auto*f=Registration::lookup(fd);return f?Registration::entries[f->owner].vfs.fstat(f->local,st):-1;}
 int close(int fd) const {return Registration::libcClose(fd);}
 int callbackClose(int fd) const {return Registration::callbackClose(fd);}
};
int main(int argc,char** argv) {
 Volume a(0x41),b(0x42);
 const auto* api=&a.api.base;
 assert(native_app_register_volume_vfs(nullptr)==ESP_ERR_INVALID_ARG);
 a.api.base.api_version=2;assert(native_app_register_volume_vfs(api)==ESP_ERR_INVALID_ARG);a.api.base.api_version=1;
 a.api.base.struct_size=sizeof(risc_storage_volume_api_v1);
 assert(native_app_register_volume_vfs(api)==ESP_ERR_INVALID_ARG);a.api.base.struct_size=sizeof(a.api);
 auto info=a.api.file_info;a.api.file_info=nullptr;
 assert(native_app_register_volume_vfs(api)==ESP_ERR_INVALID_ARG);a.api.file_info=info;
 a.ready=false;assert(native_app_register_volume_vfs(api)==ESP_ERR_INVALID_STATE);a.ready=true;
 Registration::result=ESP_ERR_NO_MEM;
 assert(native_app_register_volume_vfs(api)==ESP_ERR_NO_MEM);Registration::result=ESP_OK;
 esp_vfs_t foreign{};foreign.open=[](const char*,int,int){errno=EACCES;return -1;};
 assert(esp_vfs_register("/chosen",&foreign,nullptr)==ESP_OK);
 assert(esp_vfs_register("/chosen",&foreign,nullptr)==ESP_OK);
 assert(native_app_register_sd_vfs()==ESP_OK);const Client sd{nullptr};
 assert(native_app_register_volume_vfs(api)==ESP_OK);const Client chosen{api};
 assert(native_app_register_volume_vfs(api)==ESP_OK);
 assert(native_app_register_volume_vfs(&b.api.base)==ESP_ERR_INVALID_STATE);
 if(argc>1) {
  const bool allocation=!std::strcmp(argv[1],"fd-allocation");
  if(!std::strcmp(argv[1],"libc-close")||!std::strcmp(argv[1],"lock-close")) {
   const int fd=chosen.open("/Apps/demo.elf",O_RDONLY,0);assert(fd==0);
   a.closeFail=!std::strcmp(argv[1],"libc-close");Probe::deny=!std::strcmp(argv[1],"lock-close");
   assert(chosen.close(fd)==-1);a.closeFail=false;Probe::deny=false;
   char c=0;assert(chosen.read(fd,&c,1)==-1&&errno==EBADF);
   // IDF lost the global mapping, but no local slot/provider may be reused.
   const int next=chosen.open("/Apps/demo.elf",O_RDONLY,0);assert(next==0);
   assert(Registration::fds[next].local==1);assert(chosen.close(next)==0);
   assert(native_app_unregister_volume_vfs(api)==ESP_ERR_INVALID_STATE);
   puts("Selected volume libc close failure: mapping lost, local slot and binding retained PASS");return 0;
  }
  a.size=allocation?64:51;a.closeFail=true;
  if(allocation)Registration::fdResult=ESP_ERR_NO_MEM;
  assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==-1&&errno==EIO);
  assert(native_app_unregister_volume_vfs(api)==ESP_ERR_INVALID_STATE);
  char c=0;assert(chosen.read(0,&c,1)==-1&&errno==EBADF);
  a.size=64;a.closeFail=false;Registration::fdResult=ESP_OK;
  const int fd=chosen.open("/Apps/demo.elf",O_RDONLY,0);assert(fd==0);
  assert(Registration::fds[fd].local==1); // Unreturned slot zero is still owned.
  assert(native_app_unregister_volume_vfs(api)==ESP_ERR_INVALID_STATE);
  assert(chosen.close(fd)==0);
  assert(native_app_unregister_volume_vfs(api)==ESP_OK&&a.opens==a.closes);
  puts("Selected volume failed-open cleanup: retained token, safe explicit retry PASS");return 0;
 }
 // A legacy handle is still owned when isOpen() cannot observe readiness.
 // It must never alias a newly opened selected-volume descriptor.
 for(bool* unavailable:{&Probe::legacyUnavailable,&Probe::legacyLockDenied}) {
  assert(sd.open("/Apps/legacy.elf",O_RDONLY,0)==0);
  *unavailable=true;
  assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==1);
  char byte=0;const unsigned selectedCalls=a.calls;
  assert(sd.read(0,&byte,1)==-1&&errno==EBADF);
  assert(sd.callbackClose(0)==-1&&errno==EBADF);
  assert(a.calls==selectedCalls); // No operation switched providers.
  *unavailable=false;
  assert(sd.read(0,&byte,1)==1&&byte==0x42);
  assert(sd.close(0)==0&&chosen.close(1)==0);
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
 assert(chosen.callbackClose(fd)==-1&&errno==ETIMEDOUT);
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
 a.closeFail=true;assert(chosen.callbackClose(fd)==-1&&errno==EIO);
 assert(native_app_unregister_volume_vfs(api)==ESP_ERR_INVALID_STATE);
 assert(chosen.open("/Apps/demo.elf",O_RDONLY,0)==-1&&errno==EMFILE);
 a.closeFail=false;assert(chosen.close(fd)==0);assert(chosen.close(2)==0);
 assert(native_app_unregister_volume_vfs(&b.api.base)==ESP_ERR_INVALID_STATE);
 Registration::result=ESP_ERR_NO_MEM;assert(native_app_unregister_volume_vfs(api)==ESP_ERR_NO_MEM);
 assert(native_app_register_volume_vfs(&b.api.base)==ESP_ERR_INVALID_STATE);
 Registration::result=ESP_OK;assert(native_app_unregister_volume_vfs(api)==ESP_OK);
 // Legacy descriptors survive selected-volume removal. A different explicit
 // provider can then be bound; no stale provider or hard-coded SD identity.
 assert(sd.read(1,bytes,1)==1&&bytes[0]==0x42);
 assert(native_app_register_volume_vfs(&b.api.base)==ESP_OK);
 assert(Client{&b.api.base}.open("/Apps/demo.elf",O_RDONLY,0)==0);
 assert(Client{&b.api.base}.read(0,bytes,1)==1&&bytes[0]==0x42&&b.opens==1);
 assert(Client{&b.api.base}.close(0)==0);assert(native_app_unregister_volume_vfs(&b.api.base)==ESP_OK);
 assert(sd.close(1)==0&&sd.close(3)==0);
 assert(a.opens==a.closes&&!Probe::held);
 assert(Registration::entries[0].used&&Registration::entries[1].used);
 assert(!Registration::entries[0].pathless&&!Registration::entries[1].pathless);
 assert(Registration::pathOpen("/chosen/demo.elf",O_RDONLY,0)==-1&&errno==EACCES);
 puts("Selected volume VFS: caller binding, provider isolation, shared four slots, size/flag/seek bounds, errors and checked lifetime PASS");
}
'''
# The fixture deliberately uses range-for initializer lists.
test = '#include <initializer_list>\n' + test
run_test(test, headers)
for scenario in ('retained-cleanup', 'fd-allocation', 'libc-close', 'lock-close'):
    run_test(test, headers, (scenario,))
