#pragma once
#include "NativeVideoBootScrub.h"
#include <cstring>

// Runs inside the existing M5GFX worker and uses its bus/DMA buffers. No second
// owner, temporary video service, renderer callback or ELF allocation cleanup.
namespace M5WispRefresh {
using namespace NativeVideoBootScrub;
constexpr unsigned kScanMs = 42;  // Same ~24Hz pulse spacing as fast video.
constexpr unsigned kDrawScans = 3;

inline unsigned shadePulses(uint8_t packed, unsigned x) {
  const unsigned level = (packed >> ((x & 1U) ? 0 : 4)) & 15U;
  // RiscRTE supplies four gray levels (0,5,10,15). Round intermediate inputs
  // to the nearest supported level, matching the fast four-gray compositor.
  return (15U - level + 2U) / 5U;
}

// Hooks: alloc/free, now (monotonic ms), pause (one scheduler tick), stopped.
// The unchanged bus API owns DMA waits. Its buffers outlive this operation.
template<class Bus, class Hooks>
bool run(Bus* bus, Hooks& hooks, const uint8_t* source, uint16_t* state,
         uint8_t* dma0, uint8_t* dma1, unsigned writeBytes,
         unsigned x, unsigned y, unsigned width, unsigned height, unsigned lutOffset) {
  if (!source || !state || !dma0 || !dma1 || !width || !height ||
      x >= kWidth || y >= kHeight || width > kWidth-x || height > kHeight-y ||
      ((x | width) & 1U) || writeBytes < kWidth/4 || writeBytes > kWidth/4+32) return false;
  constexpr size_t targetBytes = kMapBytes/2;
  uint8_t* memory = hooks.alloc(kMapBytes + targetBytes);
  if (!memory) return false;  // Caller retains the original waveform fallback.
  uint8_t* target = memory + kMapBytes;
  const auto cleanup = [&]() { hooks.free(memory); };
  const uint32_t begun = hooks.now();
  uint32_t checkpoint = begun;
  for (unsigned row=0; row<kHeight; row+=kGrid) {
    if (hooks.stopped() || uint32_t(hooks.now()-begun)>=1500U) {cleanup();return false;}
    buildBand(memory,row);
    if ((row%32U)==0 || uint32_t(hooks.now()-checkpoint)>=8U) {
      hooks.pause();checkpoint=hooks.now();
    }
  }
  // Freeze the requested targets throughout all three gray draw pulses. New
  // queued writes may change source while this worker finishes the old target.
  std::memcpy(target,source,targetBytes);
  if (!bus->powerControl(true)) {cleanup();return false;}
  const uint32_t scanBegun=hooks.now();
  for (unsigned scan=0; scan<kScans+kDrawScans; ++scan) {
    if (hooks.stopped() || uint32_t(hooks.now()-scanBegun)>=2000U) {cleanup();return false;}
    const uint32_t frameStart=hooks.now();
    for (unsigned row=0; row<kHeight; ++row) {
      uint8_t* dma=(row&1U)?dma1:dma0;
      // At most the previous row is in flight, so alternating storage is safe.
      std::memset(dma,0,writeBytes);
      if (row>=y && row<y+height) {
        for (unsigned px=x; px<x+width; ++px) {
          unsigned drive;
          if (scan<kScans) drive=command(memory[row*kWidth+px],scan);
          else drive=(scan-kScans < shadePulses(target[(row*kWidth+px)/2],px))?1U:0U;
          dma[px/4] |= static_cast<uint8_t>(drive << (6U-2U*(px&3U)));
        }
      }
      if (row==0) bus->beginTransaction(); else bus->scanlineDone();
      bus->writeScanLine(dma,writeBytes);
    }
    bus->scanlineDone();
    bus->endTransaction();  // Drains the last DMA before completion/state commit.
    do {hooks.pause();} while (!hooks.stopped() && uint32_t(hooks.now()-frameStart)<kScanMs);
  }
  if (hooks.stopped()) {cleanup();return false;}
  for (unsigned row=y; row<y+height; ++row) {
    for (unsigned px=x; px<x+width; px+=2) {
      const unsigned index=row*kWidth+px;
      const uint16_t settled=static_cast<uint16_t>(0x8000U|(lutOffset<<8)|target[index/2]);
      state[index]=state[index+1]=settled;
    }
  }
  cleanup();
  return true;
}
}  // namespace M5WispRefresh
