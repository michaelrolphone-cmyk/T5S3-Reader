#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(ESP_PLATFORM) || defined(ESP32)
#include <esp_attr.h>
#define NATIVE_MONO_HOT IRAM_ATTR __attribute__((noinline))
#else
#define NATIVE_MONO_HOT __attribute__((noinline))
#endif

// Two pixels: eight state bits + two target bits = 1024 possible transitions.
// Built once before scanning; keep the 3KB instance in internal RAM.
// Includes a 1KB canonical settled-state table for eight-pixel skip tests. Entries:
// next state [7:0], drive nibble [11:8], changed [12], unfinished [13].
struct NativeVideoMonoTable {
  uint16_t entries[1024];
  uint32_t settled[256];
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
    for(unsigned pixels=0;pixels<256;++pixels) {
      settled[pixels]=0xfcfcfcfcu|(pixels>>6)|(((pixels>>4)&3)<<8)|
          (((pixels>>2)&3)<<16)|((pixels&3)<<24);
    }
  }
  // State holds four bytes per source byte and must be 4-byte aligned.
  // Heap allocations and the 480-byte state row stride satisfy this in video.
  // Keep this bounded converter in IRAM: app/PSRAM traffic must not evict its
  // instruction stream. The table instance itself is explicitly in DRAM.
  NATIVE_MONO_HOT unsigned row(const uint8_t *source,uint8_t *state,uint8_t *drive,size_t bytes) const {
    unsigned flags=0;
    state=static_cast<uint8_t *>(__builtin_assume_aligned(state,4));
    for(size_t i=0;i<bytes;++i) {
      unsigned pixels=source[i];
      uint32_t old; memcpy(&old,state,sizeof(old));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      old=__builtin_bswap32(old);
#endif
      // No lookups or PSRAM writes for an unchanged, fully settled group.
      // Exact canonical state comparison also keeps malformed/startup states
      // on the ordinary transition path rather than incorrectly skipping them.
      if(old==settled[pixels]) {
        drive[0]=drive[1]=0; state+=4; drive+=2; continue;
      }
      unsigned a=entries[((old&255)<<2)|(pixels>>6)];
      unsigned b=entries[(((old>>8)&255)<<2)|((pixels>>4)&3)];
      unsigned c=entries[(((old>>16)&255)<<2)|((pixels>>2)&3)];
      unsigned d=entries[((old>>24)<<2)|(pixels&3)];
      uint32_t next=(a&255)|((b&255)<<8)|((c&255)<<16)|(d<<24);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      next=__builtin_bswap32(next);
#endif
      memcpy(state,&next,sizeof(next)); state+=4;
      drive[0]=static_cast<uint8_t>(((a>>4)&0xf0)|((b>>8)&15));
      drive[1]=static_cast<uint8_t>(((c>>4)&0xf0)|((d>>8)&15)); drive+=2;
      flags|=a|b|c|d;
    }
    return (flags>>12)&3;
  }
};

#undef NATIVE_MONO_HOT
