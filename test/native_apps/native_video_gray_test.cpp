#include "../../src/native/NativeVideoGray.h"

#include <assert.h>
#include <stdint.h>

// Independent commanded-panel model: integrate each emitted drive code and
// compare against the pixel state, including a new target on every scan.
static void checkInterruptedTransitions() {
  for (unsigned initial = 0; initial < 4; ++initial) {
    for (unsigned sequence = 0; sequence < 4096; ++sequence) {
      uint8_t state[4];
      int physical[4];
      for (unsigned p = 0; p < 4; ++p) {
        physical[p] = (initial + p) & 3U;
        state[p] = static_cast<uint8_t>(physical[p] * 5);
      }
      uint8_t drive[1] = {};
      unsigned targets = sequence;
      uint8_t source[1] = {};
      for (unsigned scan = 0; scan < 9; ++scan) {
        // Six independently changing targets, then three scans to settle.
        if (scan < 6) {
          source[0] = static_cast<uint8_t>((targets & 3U) * 0x55U);
          targets >>= 2U;
        }
        const bool pending = nativeVideoBuildGrayRow(source, state, drive, 1);
        bool expectedPending = false;
        for (unsigned p = 0; p < 4; ++p) {
          const unsigned command = (drive[0] >> (6U - 2U*p)) & 3U;
          assert(command != 3U);
          physical[p] += command == 1U ? 1 : command == 2U ? -1 : 0;
          assert(physical[p] >= 0 && physical[p] <= 3);
          assert(static_cast<int>(state[p] & 3U) == physical[p]);
          expectedPending |= physical[p] != static_cast<int>(source[0] & 3U);
          if (scan == 8) assert(physical[p] == static_cast<int>(source[0] & 3U));
        }
        assert(pending == expectedPending);
      }
    }
  }
}

static void checkAdmission() {
  for (unsigned bits=0; bits<16; ++bits) {
    bool running=bits&1, flip=bits&2, gray=bits&4, drive=bits&8;
    assert(nativeVideoCanQueueFrame(running,flip,gray,drive) ==
           (running && !flip && !(gray && drive)));
  }
  // Queued -> first/second scan -> last DMA done -> next frame allowed.
  assert(!nativeVideoCanQueueFrame(true,true,true,true));
  assert(!nativeVideoCanQueueFrame(true,false,true,true));
  assert(nativeVideoCanQueueFrame(true,false,true,false));
  assert(nativeVideoCanQueueFrame(true,false,false,true));
}

int main() {
  checkInterruptedTransitions();
  checkAdmission();
  // Four different solid shades packed left-to-right, starting from white.
  const uint8_t target[] = {0x1b};
  uint8_t state[4] = {};
  uint8_t drive[1] = {};
  assert(nativeVideoBuildGrayRow(target, state, drive, 1));
  assert(drive[0] == 0x15); // hold white, then one black pulse each
  assert(nativeVideoBuildGrayRow(target, state, drive, 1));
  assert(drive[0] == 0x05); // light gray completed
  assert(!nativeVideoBuildGrayRow(target, state, drive, 1));
  assert(drive[0] == 0x01); // black completes on the third scan
  assert(!nativeVideoBuildGrayRow(target, state, drive, 1));
  assert(drive[0] == 0x00); // unchanged shades require no extra scan

  // Reversing the tones sends white drive, not an unbounded reset cycle.
  const uint8_t white[] = {0x00};
  assert(nativeVideoBuildGrayRow(white, state, drive, 1));
  assert(drive[0] == 0x2a);
  assert(nativeVideoBuildGrayRow(white, state, drive, 1));
  assert(drive[0] == 0x0a);
  assert(!nativeVideoBuildGrayRow(white, state, drive, 1));
  assert(drive[0] == 0x02);
}
