#pragma once
#include <stddef.h>
#include <stdint.h>

// Two pixels: eight state bits + two target bits = 1024 possible transitions.
// Built once before scanning; keep the 2KB instance in internal RAM. Entries:
// next state [7:0], drive nibble [11:8], changed [12], unfinished [13].
struct NativeVideoMonoTable {
  uint16_t entries[1024];
  void init() {
    static const uint8_t reset[4]={0xfc,0xe0,0x1c,0};
    for(unsigned old=0;old<256;++old) for(unsigned dir=0;dir<4;++dir) {
      unsigned diff=(old^dir)&3;
      uint8_t state=static_cast<uint8_t>((old&reset[diff])|dir);
      unsigned drive=(state&0x80?0:(dir&2?4:8))|(state&0x10?0:(dir&1?1:2));
      state=static_cast<uint8_t>(state+((~state>>2)&0x24));
      if(((state>>5)&7)>=3) state|=0x80;
      if(((state>>2)&7)>=3) state|=0x10;
      entries[old*4+dir]=static_cast<uint16_t>(state|(drive<<8)|
          (diff?0x1000:0)|((state&0x90)!=0x90?0x2000:0));
    }
  }
  unsigned row(const uint8_t *source,uint8_t *state,uint8_t *drive,size_t bytes) const {
    unsigned flags=0;
    for(size_t i=0;i<bytes;++i) {
      unsigned pixels=source[i];
      unsigned a=entries[(state[0]<<2)|(pixels>>6)];
      unsigned b=entries[(state[1]<<2)|((pixels>>4)&3)];
      unsigned c=entries[(state[2]<<2)|((pixels>>2)&3)];
      unsigned d=entries[(state[3]<<2)|(pixels&3)];
      state[0]=a; state[1]=b; state[2]=c; state[3]=d; state+=4;
      drive[0]=static_cast<uint8_t>(((a>>4)&0xf0)|((b>>8)&15));
      drive[1]=static_cast<uint8_t>(((c>>4)&0xf0)|((d>>8)&15)); drive+=2;
      flags|=a|b|c|d;
    }
    return (flags>>12)&3;
  }
};
