#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
struct TestStorage;
#include <Arduino.h>
#include <HalReadBudget.h>
inline uint64_t readCalls=0,readBytes=0,readLocks=0;
inline std::vector<uint64_t> trace;
inline size_t errorOffset=SIZE_MAX, maxProviderRead=SIZE_MAX;
inline bool providerError=false, ready=true, failLock=false;
inline unsigned providerMillis=0;
inline size_t mediaLossOffset=SIZE_MAX, slowOffset=0;

inline bool mediaReady(){return ready;}
constexpr size_t kMaxOperationBytes=16u*1024u*1024u;
constexpr uint32_t kOperationMs=20000;
constexpr size_t RISC_STORAGE_VOLUME_IO_MAX=4096;
class HalStorage {public:struct StorageLock {StorageLock(){++readLocks;}explicit operator bool()const{return !failLock;}};};
class HalFile {
 public:
 struct Impl{uint32_t handle=0;bool directory=false;uint8_t error=0;};
 std::unique_ptr<Impl> impl;
 HalFile()=default;HalFile(const HalFile&)=delete;HalFile&operator=(const HalFile&)=delete;
 ~HalFile(){close();}
 int read(void* out,size_t count);
 int readWithBudget(void*,size_t,HalReadBudget*);
 int readCooperatively(void*,size_t,HalReadBudget&);
 size_t write(const uint8_t*,size_t){return 0;}
 explicit operator bool()const{return data!=nullptr;}
 size_t size()const{return data?data->size():0;}
 void close();
 TestStorage* owner=nullptr;const std::vector<uint8_t>* data=nullptr;size_t position=0;
};
using FsFile=HalFile;
struct TestStorage{
 std::map<std::string,std::vector<uint8_t>> files;
 std::map<uint32_t,HalFile*> handles;
 uint32_t nextHandle=1;unsigned openFiles=0;
 bool exists(const char*path)const{return files.count(path);}
 bool openFileForRead(const char*,const std::string&path,HalFile&file){
  file.close();auto it=files.find(path);if(it==files.end())return false;
  file.owner=this;file.data=&it->second;file.position=0;
  file.impl=std::make_unique<HalFile::Impl>();file.impl->handle=nextHandle++;
  handles[file.impl->handle]=&file;++openFiles;return true;
 }
};
extern TestStorage Storage;
struct VolumeApi{
 void*context=nullptr;
 size_t(*file_read)(void*,uint32_t,uint8_t*,size_t)=nullptr;
};
inline size_t fileRead(void*,uint32_t handle,uint8_t*out,size_t count){
 ++readCalls;auto*file=Storage.handles.at(handle);
 trace.push_back(file->position);trace.push_back(count);if(file->position>=slowOffset)clockMs+=providerMillis;
 if(file->position+count>errorOffset){providerError=true;trace.push_back(0);return 0;}
 const size_t actual=std::min({count,file->data->size()-file->position,maxProviderRead});
 trace.push_back(actual);
 std::memcpy(out,file->data->data()+file->position,actual);file->position+=actual;readBytes+=actual;if(file->position>=mediaLossOffset)ready=false;return actual;
}
inline VolumeApi fakeVolume{nullptr,fileRead};inline VolumeApi*volume=&fakeVolume;
struct ExtendedApi{bool(*handle_error)(void*,uint32_t,bool);};
inline bool noError(void*,uint32_t,bool){return providerError;}
inline ExtendedApi fakeExtended{noError};inline ExtendedApi*extended=&fakeExtended;
inline void HalFile::close(){if(owner){owner->handles.erase(impl->handle);--owner->openFiles;}owner=nullptr;data=nullptr;position=0;impl.reset();}
