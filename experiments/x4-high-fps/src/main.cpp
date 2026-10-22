#ifdef X4_HIGH_FPS_LAB
#include "../source/lab_part_0.inc"
#include "../source/lab_part_1.inc"
#include "../source/lab_part_2.inc"

// Rename the stable v0.1.5 wait implementations while preserving the exact
// split-token boundary between lab_part_2 and lab_part_3.
#define wait_busy_cycle x4lab_raw_wait_busy_cycle
#define wait_refresh x4lab_raw_wait_refresh
#include "../source/lab_part_3.inc"
#undef wait_refresh
#undef wait_busy_cycle

// lab_part_3 ends inside set_partial_window(); ghost_part_4 begins by closing
// it. Function-like macros emit no tokens at this boundary and wrap each later
// hardware wait at its call site, rejecting the false 1-2 us completions seen
// in earlier laboratory runs.
#define wait_busy_cycle(timeout_ms, stage, duration_us)                                                   \
  ([&]() -> bool {                                                                                        \
    uint32_t x4lab_measured_us = 0;                                                                       \
    if (!x4lab_raw_wait_busy_cycle((timeout_ms), (stage), &x4lab_measured_us)) return false;              \
    if (x4lab_measured_us < 1000U) {                                                                      \
      Serial.printf("[X4LAB] REJECTED implausibly short BUSY cycle: stage=%s duration=%lu us\\n",       \
                    (stage), static_cast<unsigned long>(x4lab_measured_us));                              \
      return false;                                                                                        \
    }                                                                                                      \
    if ((duration_us) != nullptr) *(duration_us) = x4lab_measured_us;                                     \
    return true;                                                                                           \
  }())
#define wait_refresh(timeout_ms, duration_us)                                                              \
  ([&]() -> bool {                                                                                        \
    uint32_t x4lab_measured_us = 0;                                                                       \
    if (!x4lab_raw_wait_refresh((timeout_ms), &x4lab_measured_us)) return false;                          \
    if (x4lab_measured_us < 5000U) {                                                                      \
      Serial.printf("[X4LAB] REJECTED implausibly short DRF BUSY cycle: duration=%lu us\\n",            \
                    static_cast<unsigned long>(x4lab_measured_us));                                       \
      return false;                                                                                        \
    }                                                                                                      \
    if ((duration_us) != nullptr) *(duration_us) = x4lab_measured_us;                                     \
    return true;                                                                                           \
  }())
#include "../source/ghost_part_4.inc"
#undef wait_refresh
#undef wait_busy_cycle

#include "../source/ghost_part_5.inc"
#endif  // X4_HIGH_FPS_LAB
