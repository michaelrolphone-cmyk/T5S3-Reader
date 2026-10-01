#pragma once

#include <stddef.h>
#include <stdint.h>

// Optical cleanup is a finite, optional supplement to normal transitions.
// It never owns the backbuffer or makes frame admission wait for cleanup.
class NativeVideoIdleCleanup {
 public:
  static constexpr uint32_t kQuietMs = 500;
  static constexpr unsigned kPhases = 16;
  static constexpr unsigned kRounds = 2;

  void reset(uint32_t now_ms) { last_change_ms_ = now_ms; step_ = 0; }
  int phase(uint32_t now_ms) const {
    if (step_ >= kPhases * kRounds || uint32_t(now_ms - last_change_ms_) < kQuietMs) return -1;
    return static_cast<int>(step_ % kPhases);
  }
  void finishScan(uint32_t now_ms, bool target_changed, int scanned_phase) {
    if (target_changed) reset(now_ms);
    else if (scanned_phase >= 0 && step_ < kPhases * kRounds) ++step_;
  }

 private:
  uint32_t last_change_ms_ = 0;
  unsigned step_ = 0;
};

inline bool nativeVideoIdleRowSelected(unsigned row, int phase) {
  return phase >= 0 && phase < 16 && (row & 3U) == static_cast<unsigned>(phase >> 2);
}

// Add at most one endpoint pulse to 1/16 of the image per scan. Existing
// transition commands win; unsettled pixels and intermediate grays are skipped.
// Reaching a black/white endpoint does not change its logical commanded level.
// Repeated pulses on gray levels 1/2 would bias the shade, so do not guess them.
inline size_t nativeVideoReinforceIdleRow(const uint8_t *source, const uint8_t *state,
                                         uint8_t *drive, size_t pixels, bool grayscale,
                                         unsigned row, int phase) {
  if (!nativeVideoIdleRowSelected(row, phase)) return 0;
  size_t reinforced = 0;
  for (size_t x = static_cast<unsigned>(phase) & 3U; x < pixels; x += 4) {
    const unsigned shift = 6U - 2U * (x & 3U);
    if (((drive[x / 4] >> shift) & 3U) != 0) continue;
    unsigned target;
    if (grayscale) {
      target = (source[x / 4] >> shift) & 3U;
      if ((target != 0 && target != 3) || state[x] != target * 5U) continue;
    } else {
      target = (source[x / 8] >> (7U - (x & 7U))) & 1U;
      const uint8_t cell = state[x / 2];
      const unsigned bit = 1U - (x & 1U);
      const uint8_t settled = (x & 1U) ? 0x10U : 0x80U;
      if (!(cell & settled) || ((cell >> bit) & 1U) != target) continue;
    }
    const unsigned pulse = target ? 1U : 2U;
    drive[x / 4] = static_cast<uint8_t>(drive[x / 4] | (pulse << shift));
    ++reinforced;
  }
  return reinforced;
}
