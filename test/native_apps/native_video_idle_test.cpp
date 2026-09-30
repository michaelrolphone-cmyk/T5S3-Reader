#include "../../src/native/NativeVideoIdle.h"
#include "../../src/native/NativeVideoGray.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void scheduler() {
  NativeVideoIdleCleanup idle;
  idle.reset(100);
  assert(idle.phase(599) == -1 && idle.phase(600) == 0);
  unsigned pulses[16] = {};
  for (unsigned step = 0; step < 32; ++step) {
    const uint32_t now = 600 + step * 42;
    const int phase = idle.phase(now);
    assert(phase == static_cast<int>(step % 16));
    ++pulses[phase];
    // Identical frame submissions do not reset progress.
    idle.finishScan(now, false, phase);
  }
  for (unsigned n : pulses) assert(n == 2);
  assert(idle.phase(100000) == -1); // No endless overdrive of a static image.
  idle.finishScan(100001, true, -1);
  assert(idle.phase(100500) == -1 && idle.phase(100501) == 0);
  for (uint32_t now = 100600; now < 105000; now += 150) {
    idle.finishScan(now, true, -1);
    assert(idle.phase(now + 149) == -1); // Moving scene at about 6.6 FPS.
  }
  idle.reset(0xffffff00U);
  assert(idle.phase(0x000000f3U) == -1 && idle.phase(0x000000f4U) == 0);
}

static void mono() {
  uint8_t source[4] = {0xa5,0x5a,0xff,0x00};
  uint8_t state[16], original[16], drive[8];
  for (unsigned x=0;x<32;x+=2) {
    const unsigned pair=(source[x/8]>>(6-(x&7)))&3;
    state[x/2]=static_cast<uint8_t>(0xfc|pair);
  }
  memcpy(original,state,sizeof(state));
  unsigned coverage[4][32] = {};
  for (int phase=0;phase<16;++phase) for (unsigned y=0;y<4;++y) {
    memset(drive,0,sizeof(drive));
    size_t count=nativeVideoReinforceIdleRow(source,state,drive,32,false,y,phase);
    assert(count==((y==static_cast<unsigned>(phase/4))?8u:0u));
    for (unsigned x=0;x<32;++x) {
      const unsigned command=(drive[x/4]>>(6-2*(x&3)))&3;
      const unsigned target=(source[x/8]>>(7-(x&7)))&1;
      const bool chosen=(x&3)==static_cast<unsigned>(phase&3) && y==static_cast<unsigned>(phase/4);
      assert(command==(chosen?(target?1u:2u):0u));
      coverage[y][x]+=command!=0;
    }
  }
  for (auto &row:coverage) for (unsigned n:row) assert(n==1);
  assert(!memcmp(original,state,sizeof(state)));
  // A normal transition always wins; unknown/unsettled state gets no extra dose.
  memset(drive,0xaa,sizeof(drive));
  assert(!nativeVideoReinforceIdleRow(source,state,drive,32,false,0,0));
  for (uint8_t byte:drive) assert(byte==0xaa);
  memset(drive,0,sizeof(drive)); memset(state,0,sizeof(state));
  assert(!nativeVideoReinforceIdleRow(source,state,drive,32,false,0,0));
  // A newly requested opposite target is never reinforced as if already settled.
  memcpy(state,original,sizeof(state));
  for (auto &byte:source) byte^=0xff;
  assert(!nativeVideoReinforceIdleRow(source,state,drive,32,false,0,0));
}

static void gray() {
  uint8_t source[]={0x1b,0xe4},state[8],drive[2]={};
  for (unsigned x=0;x<8;++x) state[x]=static_cast<uint8_t>(((source[x/4]>>(6-2*(x&3)))&3)*5);
  uint8_t original[8]; memcpy(original,state,sizeof(state));
  for (int phase=0;phase<4;++phase) {
    memset(drive,0,sizeof(drive));
    nativeVideoReinforceIdleRow(source,state,drive,8,true,0,phase);
    for (unsigned x=0;x<8;++x) {
      unsigned target=(source[x/4]>>(6-2*(x&3)))&3;
      unsigned command=(drive[x/4]>>(6-2*(x&3)))&3;
      bool chosen=(x&3)==static_cast<unsigned>(phase) && (target==0 || target==3);
      assert(command==(chosen?(target?1u:2u):0u));
    }
  }
  assert(!memcmp(original,state,sizeof(state)));
  bool changed=false;
  memset(drive,0,sizeof(drive));
  assert(!nativeVideoBuildGrayRow(source,state,drive,2,&changed) && !changed);
  source[0]=0xff;
  assert(nativeVideoBuildGrayRow(source,state,drive,2,&changed) && changed);
  uint8_t normal[2]; memcpy(normal,drive,sizeof(drive));
  nativeVideoReinforceIdleRow(source,state,drive,8,true,0,0);
  for (unsigned x=0;x<8;++x) {
    unsigned shift=6-2*(x&3),before=(normal[x/4]>>shift)&3;
    if(before) assert(((drive[x/4]>>shift)&3)==before);
  }
  // Unknown startup cells must never be treated as settled black.
  memset(state,255,sizeof(state)); memset(drive,0,sizeof(drive));
  assert(!nativeVideoReinforceIdleRow(source,state,drive,8,true,0,0));
}
int main() {
  scheduler();mono();gray();
  for(unsigned bits=0;bits<16;++bits)
    assert(nativeVideoCanQueueFrame(bits&1,bits&2,bits&4,bits&8)==(bool(bits&1)&&!bool(bits&2)));
  puts("Idle video cleanup: quiet timing, finite dose, wraparound, sparse coverage, endpoints, transition priority and admission PASS");
}
