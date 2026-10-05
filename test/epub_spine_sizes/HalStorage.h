#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <freertos/task.h>
struct Counts { uint64_t reads=0, bytes=0, entries=0, seeks=0, opens=0, closes=0, yields=0, headers=0; };
extern Counts counts;
extern uint32_t clockMs, readMs;
extern std::string archivePath, fault;
extern int liveArchives, liveTemps;
uint32_t millis();
extern int failReadCountdown, readFailure, failSeekCountdown;
class Print { public: virtual ~Print()=default; virtual size_t write(const uint8_t*,size_t)=0; };
struct MemFile { std::vector<uint8_t> bytes; int handles=0; };
class FsFile : public Print {
 public:
 std::shared_ptr<MemFile> data; size_t pos=0; bool archive=false, temp=false;
 ~FsFile(){close();}
 explicit operator bool() const { return bool(data); }
 size_t size() const { return data?data->bytes.size():0; }
 size_t position() const { return pos; }
 size_t available() const { return pos<size()?size()-pos:0; }
 bool seek(uint64_t p) {
   if(archive){++counts.seeks;if(failSeekCountdown>0&&--failSeekCountdown==0)return false; if(fault=="seek"&&counts.headers>0){fault.clear();return false;}}
   if(p>size()){return false;}
   pos=p;return true;
 }
 bool seekCur(int64_t delta){ return seek(pos+delta); }
 __attribute__((noinline)) int read(void* dst,size_t n) {
   if(!data)return -1;
   size_t got=std::min(n,available());
   if(archive){++counts.reads;counts.bytes+=got;clockMs+=readMs;if(n==46)++counts.headers;
     if((n==4||n==46)&&got>=4&&data->bytes[pos]==0x50&&data->bytes[pos+1]==0x4b&&data->bytes[pos+2]==1&&data->bytes[pos+3]==2)++counts.entries;
     if(failReadCountdown>0&&--failReadCountdown==0){if(readFailure<0)return readFailure;got=std::min(got,size_t(readFailure));}
     if(n==46&&fault=="metadata"){fault.clear();return 0;}
     if(n==30&&fault=="header"){fault.clear();return 0;}
   }
   if(got){std::memcpy(dst,data->bytes.data()+pos,got);}
   pos+=got;return int(got);
 }
 size_t write(uint8_t byte){return write(&byte,1);}
 size_t write(const uint8_t* src,size_t n) override {
   if(!data)return 0;
   if(temp&&fault=="write"){fault.clear();return 0;}
   if(pos+n>size())data->bytes.resize(pos+n);
   if(n){std::memcpy(data->bytes.data()+pos,src,n);}
   pos+=n;return n;
 }
 bool close(){if(data){--data->handles;if(archive){++counts.closes;--liveArchives;}if(temp)--liveTemps;}data.reset();pos=0;return true;}
};
struct StorageFixture {
 std::map<std::string,std::shared_ptr<MemFile>> files;
 bool exists(const char* p){return files.count(p)!=0;}
 bool openFileForRead(const char*,const std::string& path,FsFile& out){
   if((path==archivePath&&fault=="open")||(path=="/cache/.tmp.css"&&fault=="temp-read")){fault.clear();return false;}
   auto it=files.find(path);if(it==files.end())return false;
   out.close();out.data=it->second;++out.data->handles;out.pos=0;out.archive=path==archivePath;out.temp=path!="/cache/book.bin" && path!=archivePath;
   if(out.archive){++counts.opens;++liveArchives;}if(out.temp){++liveTemps;}
   return true;
 }
 bool openFileForWrite(const char*,const std::string& path,FsFile& out){
   if(path=="/cache/.tmp.css"&&fault=="temp-write"){fault.clear();return false;}
   out.close();out.data=std::make_shared<MemFile>();files[path]=out.data;++out.data->handles;out.pos=0;out.archive=false;out.temp=path!="/cache/book.bin" && path!=archivePath;
   if(out.temp){++liveTemps;}
   return true;
 }
 bool remove(const char* p){auto it=files.find(p);if(it==files.end())return false;assert(it->second->handles==0);return files.erase(p)>0;}
};
extern StorageFixture Storage;
