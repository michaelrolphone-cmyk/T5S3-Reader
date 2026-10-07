#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <chrono>
inline uint64_t reads=0, bytes=0, opens=0, seeks=0, closes=0, liveFiles=0;
inline int failRead=0, failShortRead=0, failSeek=0;
inline bool failOpen=false;
inline unsigned long clockMs=0, clockStep=0;
inline bool realClock=false;
inline unsigned yields=0;
inline unsigned long millis() {
  if(realClock)return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  const auto now=clockMs; clockMs+=clockStep; return now;
}
class FsFile {
  FILE* file=nullptr;
 public:
  ~FsFile() { close(); }
  bool open(const char* path) { ++opens; close(); if(failOpen)return false; file=std::fopen(path,"rb"); if(file)++liveFiles; return file; }
  int read(uint8_t* buffer,size_t count) {
    ++reads;
    if(failRead && --failRead==0)return -1;
    if(failShortRead && --failShortRead==0 && count) --count;
    const int n=file?static_cast<int>(std::fread(buffer,1,count,file)):-1;
    if(n>0)bytes+=n;
    return n;
  }
  bool seekSet(uint32_t offset) { ++seeks; if(failSeek && --failSeek==0)return false; return file && std::fseek(file,offset,SEEK_SET)==0; }
  void close() { if(file){std::fclose(file);++closes;--liveFiles;}file=nullptr; }
};
struct TestStorage {
  std::string root;
  bool openFileForRead(const char*,const char* path,FsFile& file) { return file.open((root+path).c_str()); }
};
inline TestStorage Storage;
