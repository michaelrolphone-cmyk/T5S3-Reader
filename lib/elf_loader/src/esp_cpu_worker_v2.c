/* CPU-owned trampoline: the completion store is the LAST descriptor access.
 * A completed ELF worker has returned into this resident function before the
 * creator can release/reuse the slot or unload provider code. The remaining
 * self-delete/deferred kernel cleanup executes resident code only. */
#include "RiscCpuWorkerV2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdlib.h>
#define WORKER_SLOTS 4u
#define MAX_GENERATION (UINT64_MAX >> 8)
typedef struct {
    uint64_t token;
    TaskHandle_t owner;
    void (*entry)(void*);
    void *argument;
    bool complete;
} worker_slot;
static worker_slot slots[WORKER_SLOTS];
static uint64_t generation;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;

static void resident_entry(void *argument) {
    worker_slot *slot=(worker_slot*)argument;
    void (*entry)(void*)=slot->entry;
    void *parameter=slot->argument;
    entry(parameter);
    slot->entry=NULL;
    slot->argument=NULL;
    /* Release/acquire pairs with join/release. NO slot access, event signal,
     * provider callback or returning to ELF is permitted after this store. */
    __atomic_store_n(&slot->complete,true,__ATOMIC_RELEASE);
    vTaskDelete(NULL);
    abort(); // Defensive resident-only path if an invalid port returns.
}
static bool task_context(void) {
    return !xPortInIsrContext() && xTaskGetCurrentTaskHandle()!=NULL;
}
static worker_slot *find(uint64_t token) {
    unsigned index=(unsigned)(token&255u);
    if(!index || index>WORKER_SLOTS) return NULL;
    worker_slot *slot=&slots[index-1];
    return slot->token==token?slot:NULL;
}
int risc_cpu_worker_start_v2(void (*entry)(void*),void *argument,
 uint32_t stack_bytes,uint32_t priority,int32_t core,risc_cpu_worker_v2 *out) {
    if(out)*out=0;
    if(!out || !entry || stack_bytes<4096 || stack_bytes>32768 || (stack_bytes&3) ||
       !priority || priority>5 || priority>=configMAX_PRIORITIES ||
       core < -1 || core>=portNUM_PROCESSORS) return RISC_CPU_WORKER_INVALID;
    if(!task_context())return RISC_CPU_WORKER_CONTEXT;
    worker_slot *slot=NULL;uint64_t token=0;
    portENTER_CRITICAL(&lock);
    if(generation<MAX_GENERATION) {
        for(unsigned i=0;i<WORKER_SLOTS;++i)if(!slots[i].token){
            slot=&slots[i];token=(++generation<<8)|(i+1);
            *slot=(worker_slot){token,xTaskGetCurrentTaskHandle(),entry,argument,false};
            break;
        }
    }
    portEXIT_CRITICAL(&lock);
    if(!slot)return RISC_CPU_WORKER_CAPACITY;
    // IDF 4.4.7 does not schedule a task on a failed create. On success the
    // entry may already have returned; do not overwrite its completion state.
    if(xTaskCreatePinnedToCore(resident_entry,"rte-worker",stack_bytes,slot,
          priority,NULL,core<0?tskNO_AFFINITY:core)!=pdPASS){
        portENTER_CRITICAL(&lock);*slot=(worker_slot){0};portEXIT_CRITICAL(&lock);
        return RISC_CPU_WORKER_ALLOCATION;
    }
    *out=token;return RISC_CPU_WORKER_OK;
}
int risc_cpu_worker_join_v2(risc_cpu_worker_v2 token,uint32_t timeout_ms) {
    if(timeout_ms>2000)return RISC_CPU_WORKER_INVALID;
    if(!task_context())return RISC_CPU_WORKER_CONTEXT;
    portENTER_CRITICAL(&lock);
    worker_slot *slot=find(token);
    int error=!slot?RISC_CPU_WORKER_STALE:
        slot->owner!=xTaskGetCurrentTaskHandle()?RISC_CPU_WORKER_CONTEXT:0;
    portEXIT_CRITICAL(&lock);
    if(error)return error;
    TickType_t began=xTaskGetTickCount();
    TickType_t budget=pdMS_TO_TICKS(timeout_ms);
    if(timeout_ms&&!budget)budget=1;
    // Only this creator can release its slot; worker completion is its only
    // concurrent mutation. Other tasks cannot join/release this descriptor.
    for(unsigned polls=0;;++polls){
        if(__atomic_load_n(&slot->complete,__ATOMIC_ACQUIRE))return 0;
        if(!timeout_ms || polls>=2000 || (TickType_t)(xTaskGetTickCount()-began)>=budget)
            return RISC_CPU_WORKER_TIMEOUT;
        vTaskDelay(1);
    }
}
int risc_cpu_worker_release_v2(risc_cpu_worker_v2 token) {
    if(!task_context())return RISC_CPU_WORKER_CONTEXT;
    portENTER_CRITICAL(&lock);
    worker_slot *slot=find(token);
    int result=!slot?RISC_CPU_WORKER_STALE:
        slot->owner!=xTaskGetCurrentTaskHandle()?RISC_CPU_WORKER_CONTEXT:
        !__atomic_load_n(&slot->complete,__ATOMIC_ACQUIRE)?RISC_CPU_WORKER_BUSY:0;
    if(!result)*slot=(worker_slot){0};
    portEXIT_CRITICAL(&lock);
    return result;
}
