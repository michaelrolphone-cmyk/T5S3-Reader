/* Host implementation of the exact opaque OS/CPU ABI1 boundary. The production
 * driver is compiled unchanged; no SDK queue layout or raw spinlock is copied. */
#include "../../Drivers/x4pro_i2c/os_cpu_v1.h"
#include "os_cpu_fake.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
struct QueueDefinition { pthread_mutex_t mutex; };
bool sd_mutex_fail_create, sd_mutex_fail_take, sd_mutex_fail_give;
void (*sd_mutex_before_give)(void);
unsigned sd_mutex_creates, sd_mutex_deletes, sd_mutex_takes, sd_mutex_gives;
static _Thread_local bool in_isr, task_missing, other_task;
static _Thread_local int task_identity[2];
void sd_mutex_set_context(bool isr, bool no_task, bool different_task) {
    in_isr = isr; task_missing = no_task; other_task = different_task;
}
struct QueueDefinition *xQueueCreateMutex(uint8_t type) {
    assert(type == 1);
    if (sd_mutex_fail_create) return 0;
    struct QueueDefinition *queue = malloc(sizeof(*queue)); assert(queue);
    pthread_mutexattr_t attr;
    assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
    assert(!pthread_mutex_init(&queue->mutex, &attr));
    assert(!pthread_mutexattr_destroy(&attr));
    ++sd_mutex_creates; return queue;
}
int xQueueSemaphoreTake(struct QueueDefinition *queue, uint32_t ticks) {
    assert(queue && !ticks); ++sd_mutex_takes;
    return !sd_mutex_fail_take && pthread_mutex_trylock(&queue->mutex) == 0;
}
int xQueueGenericSend(struct QueueDefinition *queue, const void *item, uint32_t ticks, int position) {
    assert(queue && !item && !ticks && !position); ++sd_mutex_gives;
    if (sd_mutex_before_give) sd_mutex_before_give();
    return !sd_mutex_fail_give && pthread_mutex_unlock(&queue->mutex) == 0;
}
void vQueueDelete(struct QueueDefinition *queue) {
    assert(queue && !pthread_mutex_trylock(&queue->mutex));
    assert(!pthread_mutex_unlock(&queue->mutex));
    assert(!pthread_mutex_destroy(&queue->mutex));
    ++sd_mutex_deletes; free(queue);
}
int xPortInIsrContext(void) { return in_isr; }
struct tskTaskControlBlock *xTaskGetCurrentTaskHandle(void) {
    return task_missing ? 0 : (struct tskTaskControlBlock *)&task_identity[other_task ? 1 : 0];
}
