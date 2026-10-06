#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <HalWriteBudget.h>
#include <HalReadBudget.h>
struct Counters {size_t writes=0,bytes=0,delays=0,yields=0,locks=0,mutations=0,syncs=0,errors=0,closes=0;};
inline Counters counts;
inline uint64_t clockMs=0,lastYield=0,maxYieldGap=0;
inline uint32_t providerMillis=0;
inline size_t shortCall=SIZE_MAX,zeroCall=SIZE_MAX,errorCall=SIZE_MAX,syncFault=SIZE_MAX;
inline bool ready=true,lockFails=false,closeFails=false,providerError=false;
inline std::vector<uint64_t> trace;
inline uint32_t millis(){return static_cast<uint32_t>(clockMs);}
inline void recordYield(){maxYieldGap=std::max(maxYieldGap,clockMs-lastYield);++clockMs;lastYield=clockMs;}
inline void delay(unsigned){++counts.delays;recordYield();}
struct FileImpl {unsigned handle=1;bool writer=true,syncWrites=false;int error=0;size_t pos=0;std::vector<uint8_t> data;};
struct Volume {void* context=nullptr;size_t (*file_write)(void*,unsigned,const void*,size_t);};
struct Extended {bool (*file_sync)(void*,unsigned);bool (*handle_error)(void*,unsigned,bool);};
extern Volume* volume;extern Extended* extended;
inline bool mediaReady(){return ready;}
struct Generations {void mutationAttempt(){++counts.mutations;}};
inline Generations generations;
constexpr size_t kMaxOperationBytes=16u*1024u*1024u;
constexpr uint32_t kOperationMs=20000;
constexpr size_t RISC_STORAGE_VOLUME_IO_MAX=4096;
class HalFile {
 public:
 std::unique_ptr<FileImpl> impl=std::make_unique<FileImpl>();
 size_t write(const void*,size_t);
 size_t writeWithBudget(const void*,size_t,HalWriteBudget*);
 size_t writeCooperatively(const void*,size_t,HalWriteBudget&);
 // Match the real out-of-line storage boundary; do not optimize fixture EOF into decoder code.
 int read(void* p,size_t n);
 // Decoding is a byte-store fixture in this write-focused suite. Read scheduling
 // is measured independently by text_page_reads with the production HAL.
 int readCooperatively(void* p,size_t n,HalReadBudget&){return read(p,n);}
 bool seek(size_t p){impl->pos=p;return true;}
 size_t position()const{return impl->pos;}
 explicit operator bool()const{return impl&&impl->handle;}
 bool close(){++counts.closes;if(closeFails)return false;impl->handle=0;return true;}
};
using FsFile=HalFile;
struct HalStorage {class StorageLock {public:StorageLock(){++counts.locks;}explicit operator bool()const{return !lockFails;}};};
