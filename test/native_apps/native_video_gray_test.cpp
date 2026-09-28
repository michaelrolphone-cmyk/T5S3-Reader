#include "../../src/native/NativeVideoGray.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

// These tests verify command history, not the panel's optical response.
static unsigned command(uint8_t packed, unsigned pixel) {
  return (packed >> (6U - 2U * pixel)) & 3U;
}

static void checkAllTransitions() {
  for (unsigned old = 0; old < 4; ++old) {
    for (unsigned target = 0; target < 4; ++target) {
      uint8_t state[4], drive[1] = {}, source[1] = {static_cast<uint8_t>(target * 0x55U)};
      for (auto &cell : state) cell = static_cast<uint8_t>(old * 5U);
      unsigned level = old, scans = 0, pulses = 0;
      bool pending;
      do {
        pending = nativeVideoBuildGrayRow(source, state, drive, 1);
        unsigned expected = level < target ? 1U : level > target ? 2U : 0U;
        assert(drive[0] == expected * 0x55U);
        if (expected) { level = expected == 1 ? level + 1 : level - 1; ++pulses; }
        for (auto cell : state) assert((cell & 3U) == level);
        assert(pending == (level != target));
        assert(++scans <= 3);
      } while (pending);
      assert(pulses == (old > target ? old - target : target - old));
      assert(!nativeVideoBuildGrayRow(source, state, drive, 1) && drive[0] == 0);
      for (auto cell : state) assert(cell == target * 5U);
    }
  }
}

static void checkInterruptedTransitions() {
  for (unsigned initial = 0; initial < 4; ++initial) {
    for (unsigned sequence = 0; sequence < 4096; ++sequence) {
      uint8_t state[4]; unsigned levels[4];
      for (unsigned p = 0; p < 4; ++p) {
        levels[p] = (initial + p) & 3U;
        state[p] = static_cast<uint8_t>(levels[p] * 5U);
      }
      uint8_t drive[1] = {}, source[1] = {};
      unsigned targets = sequence;
      for (unsigned scan = 0; scan < 9; ++scan) {
        if (scan < 6) {
          source[0] = static_cast<uint8_t>((targets & 3U) * 0x55U);
          targets >>= 2U;
        }
        const unsigned target = source[0] & 3U;
        const bool pending = nativeVideoBuildGrayRow(source, state, drive, 1);
        bool expected_pending = false;
        for (unsigned p = 0; p < 4; ++p) {
          const unsigned expected = levels[p] < target ? 1U : levels[p] > target ? 2U : 0U;
          assert(command(drive[0], p) == expected);
          if (expected) levels[p] = expected == 1 ? levels[p] + 1 : levels[p] - 1;
          assert((state[p] & 3U) == levels[p]);
          expected_pending |= levels[p] != target;
        }
        assert(pending == expected_pending);
        if (scan == 8) assert(!pending);
      }
    }
  }
}

static void checkUnknownAndMixedRow() {
  uint8_t state[4] = {255,255,255,255}, drive[1] = {}, source[1] = {0x1b};
  for (unsigned scan = 0; scan < 6; ++scan) {
    bool pending = nativeVideoBuildGrayRow(source,state,drive,1);
    if (scan < 3) assert(drive[0] == 0xaa);
    assert(pending == (scan < 5));
  }
  for (unsigned p = 0; p < 4; ++p) assert((state[p] & 3U) == p);
  // Character moves off gray background: black -> light gray takes two white
  // pulses, no additional white erase or black redraw. Neighbor stays idle.
  source[0] = 0x55;
  for (unsigned scan = 0; scan < 3; ++scan) {
    nativeVideoBuildGrayRow(source,state,drive,1);
    assert(command(drive[0], 1) == 0);
    assert(command(drive[0], 3) == (scan < 2 ? 2U : 0U));
  }
  // Retargeting during startup cannot restart or skip the endpoint reset.
  for (auto &cell : state) cell = kNativeVideoGrayUnknown;
  for (unsigned scan = 0; scan < 3; ++scan) {
    source[0] = static_cast<uint8_t>(scan * 0x55U);
    assert(nativeVideoBuildGrayRow(source,state,drive,1));
    assert(drive[0] == 0xaa);
  }
  source[0] = 0;
  assert(!nativeVideoBuildGrayRow(source,state,drive,1) && drive[0] == 0);
}

int main() {
  for (unsigned bits=0; bits<16; ++bits) {
    bool running=bits&1, flip=bits&2, gray=bits&4, drive=bits&8;
    assert(nativeVideoCanQueueFrame(running,flip,gray,drive) == (running && !flip));
  }
  checkAllTransitions();
  checkInterruptedTransitions();
  checkUnknownAndMixedRow();
  puts("Grayscale: direct transitions, interrupted command history, startup reset, unchanged neighbors and frame admission PASS");
}
