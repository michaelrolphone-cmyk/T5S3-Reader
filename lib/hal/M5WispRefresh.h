#pragma once
#include "NativeVideoBootScrub.h"
#include <cstring>

// Runs inside the existing M5GFX worker and uses its bus/DMA buffers. No second
// owner, temporary video service, renderer callback or ELF allocation cleanup.
namespace M5WispRefresh {
using namespace NativeVideoBootScrub;
constexpr unsigned kScanMs = 42;  // Same ~24Hz pulse spacing as fast video.
constexpr unsigned kDrawScans = 3;

// Compose a whole curl in the rectangle's own coordinates. Narrow regions
// gradually become flowing ribbons, rather than a severely stretched vortex.
// Only arrival order changes: command(), cadence and endpoint dose are shared.
inline uint16_t regionField(unsigned x, unsigned y, unsigned width, unsigned height) {
  const float nx = width > 1 ? 2.0f*x/(width-1)-1.0f : 0.0f;
  const float ny = height > 1 ? 2.0f*y/(height-1)-1.0f : 0.0f;
  const float u = width >= height ? nx : ny;
  const float v = width >= height ? ny : nx;
  const float aspect = width >= height ? float(width)/height : float(height)/width;
  const float dx = u + 0.16f*std::sin(3.0f*v);
  const float dy = v - 0.16f*std::sin(2.0f*u);
  const float radius = std::sqrt(dx*dx+dy*dy);
  const float angle = std::atan2(dy,dx);
  const float curl = std::sin(2.0f*angle + 4.5f*radius + 0.5f*std::sin(3.0f*u));
  const float waves = aspect < 4.0f ? aspect : 4.0f;
  const float ribbon = std::sin(waves*3.0f*u + 2.1f*v + 1.4f*std::sin(3.0f*v+2.0f*u));
  const float blend = aspect > 3.0f ? 1.0f : (aspect-1.0f)/2.0f;
  const float t = 0.5f + 0.49f*((1.0f-blend)*curl+blend*ribbon);
  return static_cast<uint16_t>(t*(kLastArrival*256.0f));
}

inline void buildRegionBand(uint8_t* map, unsigned x, unsigned y,
                            unsigned width, unsigned height, unsigned row) {
  // Small rectangles need finer samples to retain their own curved contours.
  const unsigned gridX = width >= 64 ? kGrid : 1;
  const unsigned gridY = height >= 64 ? kGrid : 1;
  const unsigned columns = (width-1)/gridX+1;
  const unsigned nextY = row+gridY < height ? row+gridY : height-1;
  uint16_t top[kGridWidth+1], bottom[kGridWidth+1];
  for (unsigned gx=0; gx<=columns; ++gx) {
    const unsigned px = gx*gridX < width ? gx*gridX : width-1;
    top[gx]=regionField(px,row,width,height);
    bottom[gx]=regionField(px,nextY,width,height);
  }
  for (unsigned sy=0; sy<gridY && row+sy<height; ++sy) {
    const unsigned spanY = nextY > row ? nextY-row : 1;
    for (unsigned px=0; px<width; ++px) {
      const unsigned gx=px/gridX, sx=px%gridX;
      const unsigned spanX = (gx+1)*gridX < width ? gridX : width-1-gx*gridX;
      const unsigned divisorX = spanX ? spanX : 1;
      const unsigned l=top[gx]*(spanY-sy)+bottom[gx]*sy;
      const unsigned r=top[gx+1]*(spanY-sy)+bottom[gx+1]*sy;
      const unsigned value=(l*(divisorX-sx)+r*sx)/(divisorX*spanY);
      map[(y+row+sy)*kWidth+x+px]=static_cast<uint8_t>((value+128)/256);
    }
  }
}

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
  const bool full = x==0 && y==0 && width==kWidth && height==kHeight;
  const unsigned gridY = full || height>=64 ? kGrid : 1;
  for (unsigned row=0; row<height; row+=gridY) {
    if (hooks.stopped() || uint32_t(hooks.now()-begun)>=1500U) {cleanup();return false;}
    if (full) buildBand(memory,row);
    else buildRegionBand(memory,x,y,width,height,row);
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
