#pragma once
#include <T5VideoApi.h>

// Scanner-owned accumulator; publish/copy the resulting snapshot under the
// existing video lock, never hold that lock across clock reads or row work.
struct NativeVideoProfile {
  uint64_t start_us=0, scan_us=0, prepare_us=0, dma_us=0, pace_us=0, rows=0;
  uint32_t samples=0;
  void reset(uint64_t now) { *this={}; start_us=now; }
  bool record(uint64_t now,uint64_t scan,uint64_t prepare,uint64_t dma,
              uint64_t pace,uint32_t active_rows,t5_video_scan_stats_v1 &out) {
    scan_us+=scan; prepare_us+=prepare; dma_us+=dma; pace_us+=pace;
    rows+=active_rows; ++samples;
    if(now-start_us<1000000u) return false;
    out.samples=samples;
    out.scan_us=static_cast<uint32_t>(scan_us/samples);
    out.prepare_us=static_cast<uint32_t>(prepare_us/samples);
    out.dma_wait_us=static_cast<uint32_t>(dma_us/samples);
    out.pace_us=static_cast<uint32_t>(pace_us/samples);
    out.active_rows=static_cast<uint32_t>(rows/samples);
    reset(now); return true;
  }
};
