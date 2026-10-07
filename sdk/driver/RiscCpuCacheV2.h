#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Task-only data-cache writeback for a stable allocated PSRAM buffer, at most
 * 1 MiB. The trusted caller owns the allocation and keeps it live and unchanged
 * through all cooperative yields AND subsequent DMA completion. Range checks
 * prove the PSRAM address interval, not allocation ownership or lifetime.
 * This is not executable-code publication; it does not invalidate I-cache.
 * 0 success; -1 invalid range/length, -2 ISR, -3 unsupported CPU port,
 * -4 cache operation failure, -5 100 ms deadline/lock contention exhausted.
 * On any error the caller must not start DMA using partially written data.
 */
int risc_cpu_cache_writeback_v2(uintptr_t address,size_t bytes);
#ifdef __cplusplus
}
#endif
