#pragma once

#include <stddef.h>
#include <stdint.h>

// Monochrome retains its existing replace-in-flight behavior. Grayscale
// transitions must drain before a different image owns the front buffer.
inline bool nativeVideoCanQueueFrame(bool running, bool flip_pending,
                                     bool grayscale, bool drive_pending) {
  return running && !flip_pending && (!grayscale || !drive_pending);
}

/* A state byte per pixel: driven level, requested level, and remaining
 * pulses. The panel bus uses 1 for black drive and 2 for white drive. Track
 * EACH pulse, including intermediate levels, rather than only committing the
 * target on the last pulse: even an interrupted transition must be reversible.
 * This is the commanded level, not a measurement of physical panel response. */
inline bool nativeVideoBuildGrayRow(const uint8_t *source, uint8_t *state,
                                    uint8_t *drive, size_t source_bytes) {
  bool pending = false;
  for (size_t byte = 0; byte < source_bytes; ++byte) {
    const uint8_t pixels = source[byte];
    uint8_t packed = 0;
    for (uint8_t pixel = 0; pixel < 4; ++pixel) {
      const uint8_t target = static_cast<uint8_t>((pixels >> (6U - 2U * pixel)) & 3U);
      uint8_t &cell = state[byte * 4U + pixel];
      uint8_t current = cell & 3U;
      uint8_t pulse = 0;
      if (current < target) {
        pulse = 0x1U;
        ++current;
      } else if (current > target) {
        pulse = 0x2U;
        --current;
      }
      const uint8_t remaining = current > target ? current - target : target - current;
      cell = static_cast<uint8_t>((remaining << 4U) | (target << 2U) | current);
      pending = pending || remaining != 0U;
      packed = static_cast<uint8_t>((packed << 2U) | pulse);
    }
    drive[byte] = packed;
  }
  return pending;
}
