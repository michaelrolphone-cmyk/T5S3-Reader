#include <cassert>
#include <cstdio>
#include "../../src/native/NativeVideoProfile.h"
int main() {
  NativeVideoProfile p;
  t5_video_scan_stats_v1 result{};
  p.reset(100);
  assert(!p.record(500100,100000,70000,10000,0,540,result));
  assert(result.samples==0);
  assert(p.record(1000100,120000,90000,20000,0,400,result));
  assert(result.samples==2 && result.scan_us==110000);
  assert(result.prepare_us==80000 && result.dma_wait_us==15000);
  assert(result.pace_us==0 && result.active_rows==470);
  // New window must not retain old totals; test sums beyond 32-bit micros.
  p.reset(0x100000000ULL);
  assert(p.record(0x100100000ULL,30000,20000,2000,12000,1,result));
  assert(result.samples==1 && result.scan_us==30000);
  assert(result.prepare_us==20000 && result.dma_wait_us==2000);
  assert(result.pace_us==12000 && result.active_rows==1);
  p.reset(0);
  assert(!p.record(10,1,1,0,0,0,result));
  assert(p.samples==1 && p.scan_us==1);
  puts("Video profiling: window isolation, means, readiness and 64-bit clock PASS");
}
