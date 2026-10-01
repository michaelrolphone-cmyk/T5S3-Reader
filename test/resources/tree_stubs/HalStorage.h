#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define O_RDONLY 0
struct TreeSd {
  std::map<std::string,bool> nodes;
  std::string readFailure, closeFailure, createFailure;
  std::vector<std::string> mutations;
  int handles=0;
};
inline TreeSd treeSd;
class HalFile {
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
  HalFile(HalFile&& o) noexcept:path_(std::move(o.path_)),children_(std::move(o.children_)),at_(o.at_),open_(o.open_){o.open_=false;}
  ~HalFile(){if(open_)--treeSd.handles;}
  bool isOpen()const{return open_;}
  bool isDirectory()const{return open_&&treeSd.nodes[path_];}
  bool close(){if(!open_)return false;open_=false;--treeSd.handles;return path_!=treeSd.closeFailure;}
  uint8_t getError()const{return path_==treeSd.readFailure?1:0;}
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
  size_t at_=0;
  bool open_=false;
};
struct TreeStorage {
  bool exists(const char* path){return treeSd.nodes.count(path);}
  HalFile open(const char* path,int){return HalFile(path);}
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
};
inline TreeStorage Storage;
