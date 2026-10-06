#include "util/DebugSerialCommand.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Minimal owning String shell. The relevant trim/substring/equals methods below
// are verbatim pinned SDK bodies; allocation failures are outside this fixture.
class String {
  std::string data;
 public:
  String() = default;
  String(const char* value) : data(value) {}
  unsigned int len() const { return data.size(); }
  const char* buffer() const { return data.c_str(); }
  char* wbuffer() { return data.data(); }
  void setLen(unsigned size) { data.resize(size); }
  void copy(const char* src, unsigned n) { data.assign(src, n); }
  String& operator+=(char c) { data += c; return *this; }
  bool startsWith(const char* s) const { return data.size() >= strlen(s) && !strncmp(buffer(),s,strlen(s)); }
  String substring(unsigned left) const { return substring(left,len()); }
  String substring(unsigned left, unsigned right) const;
  void trim();
  bool equals(const char*) const;
  bool operator==(const char* s) const { return equals(s); }
};
struct Event { uint64_t when; uint8_t value; };
struct Queue { std::vector<Event> events; size_t at=0; } queue;
Queue* rx_queue=&queue;
uint64_t now=0, reads=0, activity=0, prints=0, writes=0;
unsigned readCost=0, failedReads=0;
bool falseAvailable=false;
uint32_t millis() { return static_cast<uint32_t>(now); }
unsigned uxQueueMessagesWaiting(Queue* q) {
  if(falseAvailable)return 1;
  unsigned n=0;
  for(size_t i=q->at;i<q->events.size() && q->events[i].when<=now;++i)++n;
  return n;
}
bool xQueueReceive(Queue* q,uint8_t* out,unsigned wait) {
  assert(wait==0); ++reads;
  if(failedReads) { --failedReads; ++now; return false; }
  if(q->at<q->events.size() && q->events[q->at].when<=now) {
    *out=q->events[q->at++].value; now+=readCost; return true;
  }
  ++now; return false;
}
class Stream {
 protected: uint32_t _startMillis=0,_timeout=1000;
 public:
  virtual int read()=0;
  virtual ~Stream()=default;
  int timedRead();
  String readStringUntil(char);
  uint32_t getTimeout()const{return _timeout;}
  void setTimeout(uint32_t n){_timeout=n;}
};
std::vector<std::string> output;
class HWCDC:public Stream {
 public:
  int available(); int read()override;
  void printf(const char* format,unsigned n) { char buf[64];snprintf(buf,sizeof(buf),format,n);output.emplace_back(buf);++prints; }
  void printf(const char* text) { output.emplace_back(text);++prints; }
  size_t write(const uint8_t* data,size_t n) { output.emplace_back(reinterpret_cast<const char*>(data),n);++writes;return n; }
}logSerial;
#include "sdk_reference.inc"
struct Display {
  uint8_t data[8]={0,1,2,3,4,5,254,255};
  uint32_t getBufferSize(){return sizeof(data);}
  uint8_t* getFrameBuffer(){return data;}
}display;
void originalService(){
#include "original_dispatch.inc"
  ++activity;
}
void currentService(){
#include "current_dispatch.inc"
  ++activity;
}
void reset(uint64_t at=0) {
  // Finish any old partial line first, leaving the exact main.cpp static state
  // clean across cases without changing its production lifetime.
  queue={};rx_queue=&queue;falseAvailable=false;failedReads=0;readCost=0;
  now+=2000;logSerial.setTimeout(0);currentService();
  queue={}; now=at;reads=activity=prints=writes=0;output.clear();logSerial.setTimeout(1000);
}
void input(const std::string& data,uint64_t gap=0) {
  for(size_t i=0;i<data.size();++i)queue.events.push_back({now+i*gap,static_cast<uint8_t>(data[i])});
}
void assertPollBound() {
  auto before=reads;auto start=now;auto dispatched=activity;currentService();
  assert(reads-before<=DebugSerialCommand::MAX_BYTES_PER_POLL);
  assert(now-start<=DebugSerialCommand::MAX_POLL_MS+readCost);
  assert(activity==dispatched+1);
}
void compare(const std::string& text) {
  reset();input(text+"\n");originalService();const auto expected=output;
  reset();input(text+"\n");
  while(queue.at<queue.events.size())assertPollBound();
  assert(output==expected);assert(writes<=1);assert(prints==writes*2);
  // Repeat after completed invalid/valid input: state must be fully reset.
  input("CMD:SCREENSHOT\n");assertPollBound();assert(writes==(expected.empty()?1u:2u));
}
void timing(const char* name,const std::string& text,uint64_t gap,uint64_t oldGap) {
  reset();input(text,gap);originalService();assert(now==oldGap);
  const auto expected=output;const auto oldReads=reads;
  reset();input(text,gap);uint64_t largest=0;
  const auto end=(text.empty()?0:(text.size()-1)*gap)+1000;
  do {auto before=now;assertPollBound();largest=std::max(largest,now-before);now+=10;}while(now<=end);
  assert(output==expected);assert(largest==0);
  std::printf("%s: original foreground gap %llu ms / %llu reads; incremental max gap %llu ms / %llu reads, %llu input dispatches\n",name,(unsigned long long)oldGap,(unsigned long long)oldReads,(unsigned long long)largest,(unsigned long long)reads,(unsigned long long)activity);
}
int main() {
#ifdef NEGATIVE_CONTROL
  reset();input("x");assertPollBound();return 1;
#endif
  static_assert(sizeof(DebugSerialCommand)<=16,"bounded per-console state");
  for(const char* s:{"","CMD:","CMD:SCREENSHOT","CMD: SCREENSHOT\r","CMD:\v\fSCREENSHOT\v\f","CMD:screenSHOT"," CMD:SCREENSHOT","CMD:SCREENSHOTx","CMD:SCRE ENSHOT"})compare(s);
  compare(std::string("CMD:SCREENSHOT\0garbage",22));
  compare(std::string("CMD:SCREENSHOT \0garbage",23));
  compare(std::string("CMD:\0SCREENSHOT",15));
  const std::string command="CMD:SCREENSHOT";
  for(size_t pos=0;pos<=command.size();++pos)for(unsigned c=0;c<256;++c) {
    if(c=='\n')continue;
    auto text=command;text.insert(pos,1,static_cast<char>(c));compare(text);
  }
  for(char a:std::string(" \t\r\v\f"))for(char b:std::string(" \t\r\v\f"))compare("CMD:"+std::string(17,a)+"SCREENSHOT"+std::string(17,b));
  compare("CMD:"+std::string(10000,' ')+"SCREENSHOT"+std::string(10000,' '));
  compare(std::string(100000,'x'));
  for(size_t split=0;split<=command.size();++split) {
    reset();input(command.substr(0,split));assertPollBound();assert(writes==0);
    now+=900;input(command.substr(split)+"\n");assertPollBound();assert(writes==1);
  }
  timing("single byte","x",0,1000);
  timing("fragmented command",command+"\n",100,1400);
  timing("slow 20 bytes",std::string(20,'x'),900,18100);
  timing("CR-only command",command+"\r",0,1000);
  timing("buffered command",command+"\n",0,0);
  reset();input(command+"\n"+command+"\n");assertPollBound();assert(writes==1);assertPollBound();assert(writes==2);
  reset();input(command);assertPollBound();now=999;assertPollBound();assert(writes==0);now=1000;assertPollBound();assert(writes==1);assertPollBound();assert(writes==1);
  reset(UINT32_MAX-500u);input(command);assertPollBound();now+=1000;assertPollBound();assert(writes==1);
  for(unsigned timeout:{0u,1u,17u,999u,1000u,2000u}) {
    reset();logSerial.setTimeout(timeout);input(command);assertPollBound();if(timeout)assert(writes==0);now+=timeout;assertPollBound();assert(writes==1);
  }
  reset();input(command);failedReads=1;assertPollBound();assert(writes==0 && queue.at==0);assertPollBound();now+=1000;assertPollBound();assert(writes==1);
  reset();input(command);assertPollBound();rx_queue=nullptr;now+=1000;assertPollBound();assert(writes==1);rx_queue=&queue;input(command+"\n");assertPollBound();assert(writes==2);
  reset();input(std::string(1000,'x')+"\n"+command+"\n");readCost=1;while(queue.at<queue.events.size())assertPollBound();assert(writes==1);
  reset();input(command.substr(0,6));assertPollBound();now+=1000;assertPollBound();input(command.substr(6)+"\n");assertPollBound();assert(writes==0);input(command+"\n");assertPollBound();assert(writes==1);
  reset();logSerial.setTimeout(5);input("CMD:SCREEN");assertPollBound();now=8;input("SHOT\n");now=10;assertPollBound();assertPollBound();assert(writes==0);
  reset();input("garbage");assertPollBound();now=1001;input(command+"\n");assertPollBound();assertPollBound();assert(writes==1);
  reset();logSerial.setTimeout(0);input("CMD:"+std::string(1000,' ')+"SCREENSHOT\n");while(queue.at<queue.events.size()){assertPollBound();now+=10;}assert(writes==1);
  std::puts("PASS: production dispatch, bounded parser, pinned SDK differential, fragmentation, timeout, rollover, read failure, disconnect/retry and cleanup");
}
