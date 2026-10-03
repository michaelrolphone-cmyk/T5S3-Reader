#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* ESP32-S3 OS/CPU ABI 3: one TX-channel reservation from the resident SDK
 * allocator also used by SPI and hardware crypto. No descriptor, transfer,
 * callback, IRQ or device/panel behavior lives behind this API.
 * Task-only, creator-owned generation tokens. Five descriptors maximum.
 * route is the SoC trigger selector (0..9, except RX-only 8); RX and memory-to-memory admission
 * are deliberately absent until a real consumer needs them. Reservation does
 * not reserve the RX sibling. Only the returned TX channel may be configured.
 * The caller owns direct channel setup/stop and must remove every IRQ/callback
 * before release. Release additionally checks hardware TX idle; it never stops
 * a live engine on the caller's behalf. Failure retains the token and clock.
 * Do not touch controller-global clock/reset: the shared SDK owns those.
 */
typedef uint64_t risc_cpu_dma_v3;
enum { RISC_CPU_DMA_OK=0,RISC_CPU_DMA_INVALID=-1,RISC_CPU_DMA_CONTEXT=-2,
 RISC_CPU_DMA_CAPACITY=-3,RISC_CPU_DMA_PLATFORM=-4,RISC_CPU_DMA_STALE=-5,
 RISC_CPU_DMA_BUSY=-6 };
/* On success channel is 0..4 in GDMA group0. On failure, a nonzero token means
 * partial cleanup retained ownership; release must succeed before unloading.
 * No caller callback is stored or IRQ installed. */
int risc_cpu_dma_reserve_tx_v3(uint32_t route,risc_cpu_dma_v3 *token,uint32_t *channel);
int risc_cpu_dma_release_v3(risc_cpu_dma_v3 token);
#ifdef __cplusplus
}
#endif
