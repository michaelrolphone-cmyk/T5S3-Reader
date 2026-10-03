#pragma once
#include <cstddef>
#include <string>

namespace ReaderStage {
constexpr size_t kLineBytes=3072,kPinsBytes=16384;
// One bounded read-only inventory pass. Hash readers must share this deadline
// and yield within their chunk loop; this helper also yields per item.
template<class Entry,size_t N,class Read,class Elapsed,class Yield>
bool verifyInventory(const Entry (&files)[N],Read read,Elapsed elapsed,Yield yield){
 if(N==0||N>128)return false;
 size_t total=0;
 for(const auto& file:files){
  if(elapsed()>=10000||file.bytes>1024u*1024u||file.bytes>4u*1024u*1024u-total)return false;
  total+=file.bytes;
  if(!read(file))return false;
  yield();
  if(elapsed()>=10000)return false;
 }
 return true;
}
// Retains at most one bounded line. After overflow discard until newline;
// never reinterpret its suffix as a new command.
class Line {
 char bytes_[kLineBytes]{};size_t used_=0;bool dropping_=false;
public:
 enum Result { Partial, Complete, Dropped };
 Result add(char c){
  if(c=='\n'){if(dropping_){reset();return Dropped;}bytes_[used_]=0;return Complete;}
  if(dropping_)return Partial;
  if(used_+1>=sizeof(bytes_)){dropping_=true;return Dropped;}
  bytes_[used_++]=c;return Partial;
 }
 void reset(){used_=0;dropping_=false;}
 char* data(){return bytes_;}
 size_t size()const{return used_;}
};
inline bool mergePins(std::string& pins){
 if(pins.size()>kPinsBytes||pins.find('\0')!=std::string::npos)return false;
 constexpr const char* names[]={"driver_manager.elf","app_store.elf"};
 bool present[2]={};size_t count=0;
 for(size_t at=0;at<pins.size();){size_t end=pins.find_first_of("\r\n",at);if(end==std::string::npos)end=pins.size();
  if(end>at){count++;for(int i=0;i<2;i++)if(pins.compare(at,end-at,names[i])==0)present[i]=true;}at=end+1;}
 for(int i=0;i<2;i++)if(!present[i]){if(++count>128)return false;if(!pins.empty()&&pins.back()!='\n')pins+='\n';pins+=names[i];pins+='\n';}
 return count<=128&&pins.size()<=kPinsBytes;
}
}
