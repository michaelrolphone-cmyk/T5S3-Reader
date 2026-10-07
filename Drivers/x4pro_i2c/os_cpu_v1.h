#pragma once
/* Exact declarations from the existing privileged OS/CPU ABI1 inventory.
 * These are not globally exported app APIs and add no ABI symbols. Keep the
 * target compile check against pinned ESP-IDF4.4.7 FreeRTOS headers enabled.
 * Queue/TCB layouts remain opaque; no SDK mutex or spinlock internals copied.
 * https://github.com/espressif/esp-idf/tree/v4.4.7/components/freertos */
#include <stdint.h>
struct QueueDefinition;
struct tskTaskControlBlock;
typedef struct QueueDefinition *x4_cpu_mutex;
typedef struct tskTaskControlBlock *x4_cpu_task;
extern struct QueueDefinition *xQueueCreateMutex(uint8_t type);
extern int xQueueSemaphoreTake(struct QueueDefinition *queue, uint32_t ticks);
extern int xQueueGenericSend(struct QueueDefinition *queue, const void *item,
                             uint32_t ticks, int position);
extern void vQueueDelete(struct QueueDefinition *queue);
extern int xPortInIsrContext(void);
extern struct tskTaskControlBlock *xTaskGetCurrentTaskHandle(void);
#if defined(__XTENSA__)
_Static_assert(sizeof(int) == 4 && sizeof(void *) == 4, "OS/CPU ABI1 requires ESP32-S3 sizes");
#endif
