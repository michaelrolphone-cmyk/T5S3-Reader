#pragma once

#include <cstddef>
#include <cstdint>
#include <esp_attr.h>

// Fixed RTC record. No allocator, locks, formatting, filesystem or SDK calls
// are used while capturing a failed CPU. Readers must accept partial traces.
namespace PanicCapture {
constexpr uint32_t kMagic = 0x50414e32;
constexpr size_t kDepth = 24;
constexpr size_t kRows = 16;
struct Frame { uint32_t pc, sp; };
struct Row { uint32_t sp, words[8]; };
struct Record {
  uint32_t magic, pc, sp, a0, ps, cause, address;
  uint32_t depth, rows, stopped;
  Frame frames[kDepth];
  Row stack[kRows];
};

inline __attribute__((always_inline)) bool readable(uint32_t address, uint32_t bytes,
                                                    uint32_t low, uint32_t high) {
  return (address & 3U) == 0 && address >= low && address < high && bytes <= high - address;
}

// Match ESP-IDF's Xtensa call-site PC normalization. Preserve raw PC/A0 above.
inline __attribute__((always_inline)) uint32_t callsite(uint32_t pc) {
  if (pc & 0x80000000U) pc = (pc & 0x3fffffffU) | 0x40000000U;
  return pc >= 3 ? pc - 3 : pc;
}

template <typename ReadWord>
void IRAM_ATTR capture(volatile Record &out, uint32_t pc, uint32_t sp, uint32_t a0,
                      uint32_t low, uint32_t high, ReadWord readWord, bool xtensa) {
  out.magic = 0;
  out.pc = pc; out.sp = sp; out.a0 = a0;
  out.depth = 0; out.rows = 0; out.stopped = 0;
  uint32_t cursor = sp;
  for (size_t i = 0; i < kRows; ++i) {
    if (!readable(cursor, 32, low, high)) break;
    out.stack[i].sp = cursor;
    for (size_t j = 0; j < 8; ++j) out.stack[i].words[j] = readWord(cursor + j * 4);
    out.rows = i + 1;
    cursor += 32;
  }
  if (xtensa) {
    out.frames[0].pc = callsite(pc); out.frames[0].sp = sp; out.depth = 1;
    uint32_t next = a0;
    while (next && out.depth < kDepth) {
      if (sp < 16 || (sp & 15U) || !readable(sp, 4, low, high) ||
          !readable(sp - 16, 16, low, high)) {
        out.stopped = 1; break;
      }
      const uint32_t callerSp = readWord(sp - 12);
      const uint32_t callerNext = readWord(sp - 16);
      // Never follow cyclic, decreasing, unaligned or non-internal stacks.
      if (callerSp <= sp || (callerSp & 15U) || !readable(callerSp, 4, low, high)) {
        out.stopped = 1; break;
      }
      const size_t i = out.depth;
      out.frames[i].pc = callsite(next); out.frames[i].sp = callerSp;
      out.depth = i + 1;
      sp = callerSp; next = callerNext;
    }
    if (next && out.depth == kDepth) out.stopped = 2;
  }
  out.magic = kMagic;
}
}  // namespace PanicCapture
