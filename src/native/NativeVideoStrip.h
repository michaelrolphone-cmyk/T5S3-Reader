#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Opt-in endpoint touch-up. Scan-thread owned; requests cross the existing
// video lock. Fixed memory, at most 64 rows, two pulses, sixteen scan deadline.
// Never changes logical state, buffer admission, or normal transition pulses.
template <size_t Rows> class NativeVideoBlackStrip {
 public:
  void reset() { memset(remaining_,0,sizeof(remaining_)); deadline_=0; }
  bool request(unsigned y,unsigned height,unsigned passes) {
    if(passes==0) {reset();return true;}
    if(passes>2 || !height || height>64 || y>=Rows || height>Rows-y)return false;
    reset();for(unsigned row=y;row<y+height;++row)remaining_[row]=static_cast<uint8_t>(passes);
    deadline_=16;return true;
  }
  bool active(unsigned y) const {return y<Rows && deadline_ && remaining_[y];}
  size_t row(unsigned y,const uint8_t *source,const uint8_t *state,uint8_t *drive,size_t pixels,bool busy) {
    if(!active(y) || busy)return 0;
    // A final normal pulse can exist even when its next state is settled.
    // Wait one scan rather than consume an extra pass without actually firing.
    for(size_t i=0;i<(pixels+3)/4;++i)if(drive[i])return 0;
    size_t count=0;
    for(size_t x=0;x<pixels;++x) {
      if(!(source[x/8]&(0x80U>>(x&7))))continue;
      const uint8_t cell=state[x/2];const unsigned bit=1U-(x&1U);
      if(!(cell&((x&1U)?0x10U:0x80U)) || !((cell>>bit)&1U))continue;
      drive[x/4]|=static_cast<uint8_t>(1U<<(6U-2U*(x&3U)));++count;
    }
    --remaining_[y];return count;
  }
  void finishScan() {if(deadline_ && !--deadline_)reset();}
 private:
  uint8_t remaining_[Rows]={};unsigned deadline_=0;
};
