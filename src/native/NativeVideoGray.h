#pragma once

#include <stddef.h>
#include <stdint.h>

/* A state byte per pixel: completed level, requested level, and remaining
 * pulses. The panel bus uses 1 for black drive and 2 for white drive. This
 * converts one packed 2bpp source row to one scan row of drive commands. */
inline bool nativeVideoBuildGrayRow(const uint8_t *source, uint8_t *state,
                                    uint8_t *drive, size_t source_bytes) {
  bool pending = false;
  for (size_t byte = 0; byte < source_bytes; ++byte) {
    const uint8_t pixels = source[byte];
    uint8_t packed = 0;
    for (uint8_t pixel = 0; pixel < 4; ++pixel) {
      const uint8_t target = static_cast<uint8_t>((pixels >> (6U - 2U * pixel)) & 3U);
      uint8_t &cell = state[byte * 4U + pixel];
      const uint8_t current = cell & 3U;
      uint8_t remaining = static_cast<uint8_t>((cell >> 4U) & 3U);
      if (((cell >> 2U) & 3U) != target) {
        // The producer cannot change frames during a pending transition.
        remaining = current > target ? current - target : target - current;
      }
      uint8_t pulse = 0;
      if (remaining != 0U) {
        pulse = target > current ? 0x1U : 0x2U;
        --remaining;
      }
      cell = static_cast<uint8_t>((remaining << 4U) | (target << 2U) |
                                  (remaining == 0U ? target : current));
      pending = pending || remaining != 0U;
      packed = static_cast<uint8_t>((packed << 2U) | pulse);
    }
    drive[byte] = packed;
  }
  return pending;
}
