#ifdef X4_HIGH_FPS_LAB
#include "../source/lab_part_0.inc"
#include "../source/lab_part_1.inc"
#include "../source/lab_part_2.inc"

bool guarded_wait_busy_cycle(uint32_t timeout_ms, const char* stage, uint32_t* duration_us);
bool guarded_wait_refresh(uint32_t timeout_ms, uint32_t* duration_us);

// Preserve the stable v0.1.5 polling implementation under private names.
#define wait_busy_cycle x4lab_raw_wait_busy_cycle
#define wait_refresh x4lab_raw_wait_refresh
#include "../source/lab_part_3.inc"
#undef wait_refresh
#undef wait_busy_cycle

// ghost_part_4 begins by closing set_partial_window(), whose definition is split
// across include files. Its hardware waits resolve to the guarded declarations.
#define wait_busy_cycle guarded_wait_busy_cycle
#define wait_refresh guarded_wait_refresh
#include "../source/ghost_part_4.inc"
#undef wait_refresh
#undef wait_busy_cycle

bool guarded_wait_busy_cycle(uint32_t timeout_ms, const char* stage, uint32_t* duration_us) {
  uint32_t measured = 0;
  if (!x4lab_raw_wait_busy_cycle(timeout_ms, stage, &measured)) return false;
  if (measured < 1000U) {
    Serial.printf("[X4LAB] REJECTED implausibly short BUSY cycle: stage=%s duration=%lu us\n", stage,
                  static_cast<unsigned long>(measured));
    return false;
  }
  if (duration_us) *duration_us = measured;
  return true;
}

bool guarded_wait_refresh(uint32_t timeout_ms, uint32_t* duration_us) {
  uint32_t measured = 0;
  if (!x4lab_raw_wait_refresh(timeout_ms, &measured)) return false;
  if (measured < 5000U) {
    Serial.printf("[X4LAB] REJECTED implausibly short DRF BUSY cycle: duration=%lu us\n",
                  static_cast<unsigned long>(measured));
    return false;
  }
  if (duration_us) *duration_us = measured;
  return true;
}

#include "../source/ghost_part_5.inc"
#endif  // X4_HIGH_FPS_LAB
