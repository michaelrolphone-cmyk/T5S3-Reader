#include "../../src/native/NativeVideoGray.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

// Deliberately asymmetric illustrative response, NOT a calibrated panel model:
// black adds 2 units, white removes 3. It falsifies the former assumption that
// opposing pulses cancel and tests whether all target shades share a baseline.
static int respond(int level, unsigned command) {
  assert(command != 3U);
  if (command == 1U) return level + 2 > 6 ? 6 : level + 2;
  if (command == 2U) return level - 3 < 0 ? 0 : level - 3;
  return level;
}

static void checkAllTransitions() {
  for (unsigned old = 0; old < 4; ++old) {
    for (unsigned target = 0; target < 4; ++target) {
      uint8_t state[4], drive[1] = {}, source[1] = {static_cast<uint8_t>(target * 0x55U)};
      for (auto &cell : state) cell = static_cast<uint8_t>(old * 5U);
      int level = old * 2;
      unsigned erases = 0, draws = 0, scans = 0;
      bool pending;
      do {
        pending = nativeVideoBuildGrayRow(source, state, drive, 1);
        const unsigned command = drive[0] & 3U;
        assert(drive[0] == command * 0x55U);
        if (command == 2U) { assert(draws == 0); ++erases; }
        if (command == 1U) ++draws;
        level = respond(level, command);
        assert(++scans <= 6);
      } while (pending);
      assert(level == static_cast<int>(target * 2));
      assert(erases == ((old != target && old != 0) ? 3U : 0U));
      assert(draws == (old != target ? target : 0U));
      assert(!nativeVideoBuildGrayRow(source, state, drive, 1) && drive[0] == 0);
      for (auto cell : state) assert(cell == target * 5U);
    }
  }
  // Demonstrate the former black -> light-gray failure under this model:
  // two white pulses left white (0), rather than light gray (2).
  assert(respond(respond(6, 2), 2) == 0);
}

static void checkInterruptedTransitions() {
  for (unsigned initial = 0; initial < 4; ++initial) {
    for (unsigned sequence = 0; sequence < 4096; ++sequence) {
      uint8_t state[4]; int levels[4];
      for (unsigned p = 0; p < 4; ++p) {
        unsigned old = (initial + p) & 3U;
        levels[p] = old * 2; state[p] = static_cast<uint8_t>(old * 5);
      }
      uint8_t drive[1] = {}, source[1] = {};
      unsigned targets = sequence;
      for (unsigned scan = 0; scan < 12; ++scan) {
        if (scan < 6) {
          source[0] = static_cast<uint8_t>((targets & 3U) * 0x55U);
          targets >>= 2U;
        }
        const bool pending = nativeVideoBuildGrayRow(source, state, drive, 1);
        for (unsigned p = 0; p < 4; ++p) {
          levels[p] = respond(levels[p], (drive[0] >> (6U - 2U*p)) & 3U);
          if (!pending) assert(levels[p] == static_cast<int>((source[0] & 3U) * 2U));
        }
        if (scan == 11) assert(!pending);
      }
    }
  }
}

static void checkUnknownAndMixedRow() {
  uint8_t state[4] = {255,255,255,255}, drive[1] = {}, source[1] = {0x1b};
  int levels[4] = {6,2,4,6};
  for (unsigned scan = 0; scan < 6; ++scan) {
    bool pending = nativeVideoBuildGrayRow(source,state,drive,1);
    if (scan < 3) assert(drive[0] == 0xaa); // Explicit startup erase, including white.
    for (unsigned p = 0; p < 4; ++p)
      levels[p] = respond(levels[p], (drive[0] >> (6U-2U*p)) & 3U);
    assert(pending == (scan < 5));
  }
  for (unsigned p = 0; p < 4; ++p) assert(levels[p] == static_cast<int>(2*p));
  // Move a black character away, exposing light gray; adjacent gray is stable.
  source[0] = 0x55;
  unsigned stable = 1;
  for (unsigned scan = 0; scan < 6; ++scan) {
    nativeVideoBuildGrayRow(source,state,drive,1);
    assert(((drive[0] >> (6U-2U*stable)) & 3U) == 0);
  }
}

int main() {
  for (unsigned bits=0; bits<16; ++bits) {
    bool running=bits&1, flip=bits&2, gray=bits&4, drive=bits&8;
    assert(nativeVideoCanQueueFrame(running,flip,gray,drive) ==
           (running && !flip && !(gray && drive)));
  }
  checkAllTransitions();
  checkInterruptedTransitions();
  checkUnknownAndMixedRow();
  puts("Grayscale: all shade transitions, asymmetric response, interrupted sequences, startup erase and unchanged pixels PASS");
}
