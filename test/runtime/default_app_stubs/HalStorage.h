#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
struct FakeStore {
  const char* data=nullptr;
  size_t size=0;
  bool mounted=true, present=false, directory=false, shortRead=false, closeOk=true;
  bool ready() const{return mounted;}
  bool exists(const char*) const{return present;}
  struct File {
    FakeStore* store=nullptr;
    bool isOpen() const{return store!=nullptr;}
    bool isDirectory() const{return store && store->directory;}
    uint64_t fileSize64() const{return store?store->size:0;}
    int read(char* out,size_t length){
      if(!store || !store->data)return -1;
      const size_t count=store->shortRead?length-1:length;
      std::memcpy(out,store->data,count);return static_cast<int>(count);
    }
    bool close(){return store && store->closeOk;}
  };
  File open(const char*,int){return present?File{this}:File{};}
};
extern FakeStore Storage;
