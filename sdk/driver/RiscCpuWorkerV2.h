#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Generic resident worker trampoline, privileged OS/CPU ABI 2 only.
 * Handles are generation tokens, not task/TCB pointers. Calls are task-only;
 * only the creator may join/release. Entry must return normally: no self-delete,
 * exceptions, longjmp out of entry, or deferred ELF TLS/deletion callbacks.
 * Join proves no future ELF entry/argument access by this worker. It does NOT
 * prove DMA/IRQ quiescence or IDF's deferred stack/TCB reclamation. Release
 * consumes only the resident descriptor; it never frees RTOS-owned memory.
 * Four descriptors maximum. Deferred RTOS reclamation is separately scheduled
 * by IDF idle tasks; this is not a four-TCB aggregate-memory guarantee.
 */
typedef uint64_t risc_cpu_worker_v2;
enum {
 RISC_CPU_WORKER_OK=0, RISC_CPU_WORKER_INVALID=-1,
 RISC_CPU_WORKER_CONTEXT=-2, RISC_CPU_WORKER_CAPACITY=-3,
 RISC_CPU_WORKER_ALLOCATION=-4, RISC_CPU_WORKER_TIMEOUT=-5,
 RISC_CPU_WORKER_STALE=-6, RISC_CPU_WORKER_BUSY=-7
};
/* stack_bytes 4096..32768, multiple of 4; priority 1..5 and below the SDK
 * maximum; core -1 (unbound), 0 or 1 if present. Failure leaves *out=0 and
 * guarantees the entry cannot later run. Entry may finish before start returns.
 */
int risc_cpu_worker_start_v2(void (*entry)(void*),void *argument,
 uint32_t stack_bytes,uint32_t priority,int32_t core,risc_cpu_worker_v2 *out);
/* timeout 0 polls, 1..2000 waits cooperatively; larger values rejected.
 * Repeated successful joins are allowed. Timeout retains descriptor and work.
 */
int risc_cpu_worker_join_v2(risc_cpu_worker_v2 worker,uint32_t timeout_ms);
/* Rejects running/stale handles. Successful release consumes the generation. */
int risc_cpu_worker_release_v2(risc_cpu_worker_v2 worker);
#ifdef __cplusplus
}
#endif
