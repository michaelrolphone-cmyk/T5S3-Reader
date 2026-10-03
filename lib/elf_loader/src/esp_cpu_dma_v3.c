/* Generic shared CPU DMA reservation, not a display/GDMA transfer backend.
 * The real SDK allocator owns channel/trigger occupancy and global clocks. */
#include "RiscCpuDmaV3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_private/gdma.h"
#include "hal/gdma_ll.h"
#include "soc/soc_caps.h"
#include <stdbool.h>
#include <stddef.h>
#if SOC_GDMA_GROUPS != 1 || SOC_GDMA_PAIRS_PER_GROUP != 5
#error "CPU DMA ABI3 requires the audited ESP32-S3 GDMA topology"
#endif
#define DMA_SLOTS 5u
#define MAX_GENERATION (UINT64_MAX >> 8)
typedef struct {
    uint64_t token;
    TaskHandle_t owner;
    gdma_channel_handle_t handle;
    int channel;
    bool connected;
} dma_slot;
static dma_slot slots[DMA_SLOTS];
static uint64_t generation;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static bool task_context(void){return !xPortInIsrContext()&&xTaskGetCurrentTaskHandle()!=NULL;}
static dma_slot *find(uint64_t token) {
    unsigned index=(unsigned)(token&255u);
    if(!index||index>DMA_SLOTS)return NULL;
    dma_slot *slot=&slots[index-1];return slot->token==token?slot:NULL;
}
int risc_cpu_dma_release_v3(risc_cpu_dma_v3 token) {
    if(!task_context())return RISC_CPU_DMA_CONTEXT;
    portENTER_CRITICAL(&lock);
    dma_slot *slot=find(token);
    int error=!slot?RISC_CPU_DMA_STALE:
        slot->owner!=xTaskGetCurrentTaskHandle()?RISC_CPU_DMA_CONTEXT:0;
    portEXIT_CRITICAL(&lock);
    if(error)return error;
    // No other task may modify this creator's descriptor. The SDK reservation
    // remains held while checking hardware and during partial release retries.
    if(slot->handle) {
        if(slot->channel>=0 && !gdma_ll_tx_is_fsm_idle(GDMA_LL_GET_HW(0),slot->channel))
            return RISC_CPU_DMA_BUSY;
        if(slot->connected) {
            if(gdma_disconnect(slot->handle)!=ESP_OK)return RISC_CPU_DMA_PLATFORM;
            slot->connected=false;
        }
        // This API never registers a DMA callback/IRQ, so SDK deletion cannot
        // enter the cross-core interrupt-free path or retain an ELF callback.
        if(gdma_del_channel(slot->handle)!=ESP_OK)return RISC_CPU_DMA_PLATFORM;
        slot->handle=NULL;
    }
    portENTER_CRITICAL(&lock);*slot=(dma_slot){0};portEXIT_CRITICAL(&lock);
    return RISC_CPU_DMA_OK;
}
int risc_cpu_dma_reserve_tx_v3(uint32_t route,risc_cpu_dma_v3*out,uint32_t*channel) {
    if(out)*out=0;
    if(channel)*channel=UINT32_MAX;
    // Selector8 is ADC/RX-only on this SoC. No memory-to-memory admission.
    if(!out||!channel||route>9||route==8)return RISC_CPU_DMA_INVALID;
    if(!task_context())return RISC_CPU_DMA_CONTEXT;
    dma_slot *slot=NULL;
    portENTER_CRITICAL(&lock);
    if(generation<MAX_GENERATION)for(unsigned i=0;i<DMA_SLOTS;++i)if(!slots[i].token) {
        slot=&slots[i];*slot=(dma_slot){(++generation<<8)|(i+1),xTaskGetCurrentTaskHandle(),NULL,-1,false};break;
    }
    portEXIT_CRITICAL(&lock);
    if(!slot)return RISC_CPU_DMA_CAPACITY;
    *out=slot->token; // All partial allocation/trigger ownership has a token.
    const gdma_channel_alloc_config_t config={.direction=GDMA_CHANNEL_DIRECTION_TX};
    int error=gdma_new_channel(&config,&slot->handle);
    if(error==ESP_OK)error=gdma_get_channel_id(slot->handle,&slot->channel);
    if(error==ESP_OK && (slot->channel<0||slot->channel>=5)) {
        slot->channel=-1;
        error=ESP_ERR_INVALID_STATE;
    }
    // On S3 the TX selector is instance_id; periph is ignored by its LL.
    const gdma_trigger_t trigger={.periph=GDMA_TRIG_PERIPH_SPI,.instance_id=(int)route};
    if(error==ESP_OK) {
        error=gdma_connect(slot->handle,trigger);
        if(error==ESP_OK)slot->connected=true;
    }
    if(error!=ESP_OK) {
        if(risc_cpu_dma_release_v3(*out)==RISC_CPU_DMA_OK)*out=0;
        return RISC_CPU_DMA_PLATFORM;
    }
    *channel=(uint32_t)slot->channel;return RISC_CPU_DMA_OK;
}
