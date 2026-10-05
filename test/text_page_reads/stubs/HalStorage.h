#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <HalReadBudget.h>
struct Counters {size_t reads=0,bytes=0,delays=0,yields=0,locks=0,seeks=0,opens=0,closes=0;};
inline Counters counts;
inline uint64_t clockMs=0,lastYield=0,maxYieldGap=0;
inline uint32_t providerMillis=0;
inline size_t maxRead=SIZE_MAX,errorCall=SIZE_MAX,zeroCall=SIZE_MAX;
inline bool providerError=false,ready=true,lockFails=false,openFails=false,closeFails=false;
inline std::vector<uint64_t> trace;
inline unsigned long millis(){return static_cast<uint32_t>(clockMs);}
inline void recordYield(){maxYieldGap=std::max(maxYieldGap,clockMs-lastYield);++clockMs;lastYield=clockMs;}
inline void delay(unsigned){++counts.delays;recordYield();}
struct FileImpl {unsigned handle=1;bool directory=false;int error=0;size_t pos=0;std::vector<uint8_t> data;bool opened=false;};
struct Volume {void* context=nullptr;size_t (*file_read)(void*,unsigned,void*,size_t);};
struct Extended {bool (*handle_error)(void*,unsigned,bool);};
extern Volume* volume;extern Extended* extended;
inline bool mediaReady(){return ready;}
constexpr size_t kMaxOperationBytes=16u*1024u*1024u;
constexpr uint32_t kOperationMs=20000;
constexpr size_t RISC_STORAGE_VOLUME_IO_MAX=4096;
class HalFile {
 public:
 std::unique_ptr<FileImpl> impl=std::make_unique<FileImpl>();
 int read(void*,size_t);
 int readWithBudget(void*,size_t,HalReadBudget*);
 int readCooperatively(void*,size_t,HalReadBudget&);
 ~HalFile(){if(impl&&impl->opened){closeFails=false;close();}}
 size_t write(const void* p,size_t n){auto& x=*impl;if(x.pos+n>x.data.size())x.data.resize(x.pos+n);if(n)memcpy(x.data.data()+x.pos,p,n);x.pos+=n;return n;}
 bool seek(size_t p){++counts.seeks;impl->pos=p;return true;}
 bool close(){if(impl->opened){++counts.closes;if(closeFails)return false;impl->opened=false;impl->handle=0;}return true;}
};
using FsFile=HalFile;
struct HalStorage {
 class StorageLock {public:StorageLock(){++counts.locks;}explicit operator bool()const{return !lockFails;}};
 std::vector<uint8_t> data;
 bool openFileForRead(const char*,const std::string&,HalFile& f){++counts.opens;if(openFails)return false;f.impl=std::make_unique<FileImpl>();f.impl->data=data;f.impl->opened=true;volume->context=f.impl.get();return true;}
};
inline HalStorage Storage;
