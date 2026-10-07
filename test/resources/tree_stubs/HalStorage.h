#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 2
#define O_EXCL 4
struct TreeSd {
  std::map<std::string,bool> nodes;
  std::map<std::string,std::vector<uint8_t>> contents;
  std::string readFailure, closeFailure, createFailure, writeFailure;
  std::vector<std::string> mutations;
  int handles=0;
};
inline TreeSd treeSd;
class HalFile {
  uint8_t metadataError_=0;
 public:
  HalFile()=default;
  explicit HalFile(std::string path):path_(path),open_(treeSd.nodes.count(path)) {
    if(open_)++treeSd.handles;
    if(open_ && treeSd.nodes[path]){
      const std::string prefix=path+"/";
      for(const auto& node:treeSd.nodes){
        if(node.first.compare(0,prefix.size(),prefix))continue;
        const auto name=node.first.substr(prefix.size());
        if(name.find('/')==std::string::npos)children_.push_back(node.first);
      }
    }
  }
  HalFile(const HalFile&)=delete;
  HalFile& operator=(const HalFile&)=delete;
  HalFile(HalFile&& o) noexcept:path_(std::move(o.path_)),children_(std::move(o.children_)),at_(o.at_),position_(o.position_),open_(o.open_){o.open_=false;}
  HalFile& operator=(HalFile&& o) noexcept {
    if(this!=&o){if(open_)--treeSd.handles;path_=std::move(o.path_);children_=std::move(o.children_);
      at_=o.at_;position_=o.position_;open_=o.open_;o.open_=false;}return *this;
  }
  uint64_t fileSize64()const{return treeSd.contents[path_].size();}
  bool seek64(uint64_t p){if(p>fileSize64())return false;position_=p;return true;}
  int read(void* out,size_t count){
    if(!open_||isDirectory()||path_==treeSd.readFailure)return -1;
    const auto& bytes=treeSd.contents[path_];count=std::min(count,bytes.size()-position_);
    std::memcpy(out,bytes.data()+position_,count);position_+=count;return static_cast<int>(count);
  }
  size_t write(const void* data,size_t count){
    if(!open_||isDirectory()||path_==treeSd.writeFailure)return 0;
    const auto* bytes=static_cast<const uint8_t*>(data);
    auto& out=treeSd.contents[path_];out.insert(out.end(),bytes,bytes+count);return count;
  }
  ~HalFile(){if(open_)--treeSd.handles;}
  bool isOpen()const{return open_;}
  bool isDirectory()const{return open_&&treeSd.nodes[path_];}
  bool close(){if(!open_)return false;open_=false;--treeSd.handles;return path_!=treeSd.closeFailure;}
  uint8_t getError()const{return metadataError_?metadataError_:path_==treeSd.readFailure?1:0;}
  struct DirectoryEntry { char name[128]{}; uint64_t size=0; bool isDirectory=false; };
  bool readDirectoryEntry(DirectoryEntry& result) {
    result={}; auto child=openNextFile();
    if(!child.isOpen()) return false;
    const size_t n=child.getName(result.name,sizeof(result.name));
    result.isDirectory=child.isDirectory();
    result.size=result.isDirectory?0:child.fileSize64();
    const bool closed=child.close();
    if(!n||n>=sizeof(result.name)||!closed){metadataError_=1;return false;}
    return true;
  }
  HalFile openNextFile(){
    if(path_==treeSd.readFailure||at_==children_.size())return HalFile{};
    return HalFile(children_[at_++]);
  }
  size_t getName(char* out,size_t capacity){
    auto name=path_.substr(path_.find_last_of('/')+1);
    if(name.size()>=capacity)return capacity;
    std::memcpy(out,name.c_str(),name.size()+1);return name.size();
  }
 private:
  std::string path_;
  std::vector<std::string> children_;
  size_t at_=0,position_=0;
  bool open_=false;
};
struct TreeStorage {
  bool exists(const char* path){return treeSd.nodes.count(path);}
  HalFile open(const char* path,int flags){
    if((flags&O_EXCL)&&exists(path))return {};
    if(!exists(path)&&(flags&O_CREAT)){
      const std::string p(path),parent=p.substr(0,p.find_last_of('/'));
      if(!exists(parent.c_str())||!treeSd.nodes[parent])return {};
      treeSd.nodes[path]=false;treeSd.contents[path]={};
    }
    return HalFile(path);
  }
  bool mkdir(const char* path,bool){
    if(exists(path)||path==treeSd.createFailure)return false;
    treeSd.nodes[path]=true;treeSd.mutations.emplace_back(path);return true;
  }
  bool remove(const char* path){
    if(!exists(path)||treeSd.nodes[path])return false;
    treeSd.nodes.erase(path);treeSd.mutations.emplace_back(path);return true;
  }
  bool rmdir(const char* path){
    if(!exists(path)||!treeSd.nodes[path])return false;
    const std::string prefix=std::string(path)+"/";
    for(const auto& n:treeSd.nodes)if(!n.first.compare(0,prefix.size(),prefix))return false;
    treeSd.nodes.erase(path);treeSd.mutations.emplace_back(path);return true;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) const {
    if (file.isOpen() && !file.close()) return false;
    file = const_cast<TreeStorage*>(this)->open(path, O_RDONLY);
    return file.isOpen() && !file.isDirectory();
  }

};
inline TreeStorage Storage;
