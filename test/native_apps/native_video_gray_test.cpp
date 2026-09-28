#include "../../src/native/NativeVideoGray.h"

#include <assert.h>
#include <stdint.h>

int main() {
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
