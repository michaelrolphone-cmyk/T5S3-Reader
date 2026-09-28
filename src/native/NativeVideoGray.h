#pragma once

#include <stddef.h>
#include <stdint.h>

// Monochrome retains its existing replace-in-flight behavior. Grayscale
// transitions must drain before a different image owns the front buffer.
inline bool nativeVideoCanQueueFrame(bool running, bool flip_pending,
                                     bool grayscale, bool drive_pending) {
  return running && !flip_pending && (!grayscale || !drive_pending);
}

// Unknown retained panel contents must be erased even when the first image is
// white. This sentinel cannot occur in a completed or in-progress transition.
constexpr uint8_t kNativeVideoGrayUnknown = 0xffU;
constexpr uint8_t kNativeVideoGrayErasePasses = 3U;

/* State: completed target [1:0], requested target [3:2], passes left [7:4].
 * Gray pulses are NOT reversible arithmetic. For a changed nonwhite pixel,
 * erase to the white endpoint, then draw the new shade from that endpoint.
 * This avoids trying to obtain a gray shade by partially whitening black.
 * Unchanged pixels get no pulses; known white pixels need only the draw phase.
 * Three erase passes reuse the existing raw-video endpoint drive duration;
 * optical settling/gray calibration still require physical panel validation. */
inline bool nativeVideoBuildGrayRow(const uint8_t *source, uint8_t *state,
                                    uint8_t *drive, size_t source_bytes) {
  bool pending = false;
  for (size_t byte = 0; byte < source_bytes; ++byte) {
    const uint8_t pixels = source[byte];
    uint8_t packed = 0;
    for (uint8_t pixel = 0; pixel < 4; ++pixel) {
      const uint8_t target = static_cast<uint8_t>((pixels >> (6U - 2U * pixel)) & 3U);
      uint8_t &cell = state[byte * 4U + pixel];
      uint8_t completed = cell & 3U;
      const uint8_t requested = (cell >> 2U) & 3U;
      uint8_t remaining = cell >> 4U;
      const bool unknown = cell == kNativeVideoGrayUnknown;
      const bool interrupted = remaining != 0U && requested != target;
      if (unknown || interrupted || (remaining == 0U && completed != target)) {
        // An interrupted draw is no longer a known white starting point, even
        // when the last completed target was white. Restart its erase phase.
        remaining = static_cast<uint8_t>(target +
            ((unknown || interrupted || completed != 0U) ? kNativeVideoGrayErasePasses : 0U));
      }
      uint8_t pulse = 0;
      if (remaining != 0U) {
        pulse = remaining > target ? 0x2U : 0x1U;
        --remaining;
        if (remaining == 0U) completed = target;
      }
      cell = static_cast<uint8_t>((remaining << 4U) | (target << 2U) | completed);
      pending = pending || remaining != 0U;
      packed = static_cast<uint8_t>((packed << 2U) | pulse);
    }
    drive[byte] = packed;
  }
  return pending;
}
