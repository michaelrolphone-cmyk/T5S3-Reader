#pragma once

#include <stddef.h>
#include <stdint.h>

// Both formats can retarget at a scan boundary. A queued flip owns the back
// buffer until consumed; outstanding pixel pulses do not own that buffer.
inline bool nativeVideoCanQueueFrame(bool running, bool flip_pending,
                                     bool /*grayscale*/, bool /*drive_pending*/) {
  return running && !flip_pending;
}

// Reserved startup states count down three white endpoint pulses. Normal
// commanded states use only bits 0..5 and cannot collide with these values.
constexpr uint8_t kNativeVideoGrayUnknown = 0xffU;
constexpr uint8_t kNativeVideoGrayStartupLast = 0xfdU;

/* A state byte per pixel: driven level, requested level, and remaining
 * pulses. The panel bus uses 1 for black drive and 2 for white drive. Track
 * EACH pulse, including intermediate levels, rather than only committing the
 * target on the last pulse: a new target continues from the pulses already sent.
 * This is the commanded level, not a measurement of physical panel response. */
inline bool nativeVideoBuildGrayRow(const uint8_t *source, uint8_t *state,
                                    uint8_t *drive, size_t source_bytes,
                                    bool *target_changed = nullptr) {
  bool pending = false;
  for (size_t byte = 0; byte < source_bytes; ++byte) {
    const uint8_t pixels = source[byte];
    uint8_t packed = 0;
    for (uint8_t pixel = 0; pixel < 4; ++pixel) {
      const uint8_t target = static_cast<uint8_t>((pixels >> (6U - 2U * pixel)) & 3U);
      uint8_t &cell = state[byte * 4U + pixel];
      if (target_changed && (cell >= kNativeVideoGrayStartupLast || ((cell >> 2U) & 3U) != target))
        *target_changed = true;
      uint8_t current = cell & 3U;
      uint8_t pulse = 0;
      if (cell >= kNativeVideoGrayStartupLast) {
        // Establish an endpoint only on startup, never during animation.
        pulse = 0x2U;
        cell = cell == kNativeVideoGrayStartupLast ? 0U : cell - 1U;
        pending = pending || cell != 0U || target != 0U;
        packed = static_cast<uint8_t>((packed << 2U) | pulse);
        continue;
      }
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
