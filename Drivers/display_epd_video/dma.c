/* Provider-local TX mechanics. The CPU port's resident SDK allocator is the
 * sole channel/trigger/clock authority shared with flash/SPI and crypto. */
#include "esp_private/gdma.h"
#include "hal/gdma_ll.h"
#include "RiscCpuDmaV3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
/* Materialize the base once: Xtensa shared linking cannot relocate an
 * absolute MMIO symbol plus a folded member offset. */
static gdma_dev_t *volatile hardware=GDMA_LL_GET_HW(0);
struct gdma_channel_t {
    risc_cpu_dma_v3 token;
    uint32_t channel;
    bool connected;
    portMUX_TYPE lock;
};
esp_err_t gdma_new_channel(const gdma_channel_alloc_config_t *config,gdma_channel_handle_t *out) {
    if(out)*out=NULL;
    if(xPortInIsrContext() || !config || !out || config->direction!=GDMA_CHANNEL_DIRECTION_TX ||
       config->sibling_chan || config->flags.reserve_sibling)return ESP_ERR_INVALID_ARG;
    gdma_channel_handle_t ch=calloc(1,sizeof(*ch));
    if(!ch)return ESP_ERR_NO_MEM;
    ch->channel=UINT32_MAX;ch->lock=(portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    *out=ch;return ESP_OK;
}
esp_err_t gdma_connect(gdma_channel_handle_t ch,gdma_trigger_t trigger) {
    if(xPortInIsrContext() || !ch || ch->token || trigger.periph!=GDMA_TRIG_PERIPH_LCD ||
       trigger.instance_id!=SOC_GDMA_TRIG_PERIPH_LCD0)return ESP_ERR_INVALID_ARG;
    const int result=risc_cpu_dma_reserve_tx_v3(trigger.instance_id,&ch->token,&ch->channel);
    if(result)return ESP_FAIL; // Partial reservation token retained for delete.
    ch->connected=true;return ESP_OK;
}
esp_err_t gdma_apply_strategy(gdma_channel_handle_t ch,const gdma_strategy_config_t *config) {
    if(!ch || !ch->connected || !config)return ESP_ERR_INVALID_ARG;
    gdma_ll_tx_enable_owner_check(hardware,ch->channel,config->owner_check);
    gdma_ll_tx_enable_auto_write_back(hardware,ch->channel,config->auto_update_desc);
    return ESP_OK;
}
esp_err_t gdma_set_transfer_ability(gdma_channel_handle_t ch,const gdma_transfer_ability_t *ability) {
    if(!ch || !ch->connected || !ability ||
       (ability->sram_trans_align & (ability->sram_trans_align-1)))return ESP_ERR_INVALID_ARG;
    int block;
    switch(ability->psram_trans_align) {
    case 0:case 16:block=GDMA_LL_EXT_MEM_BK_SIZE_16B;break;
    case 32:block=GDMA_LL_EXT_MEM_BK_SIZE_32B;break;
    case 64:block=GDMA_LL_EXT_MEM_BK_SIZE_64B;break;
    default:return ESP_ERR_INVALID_ARG;
    }
    gdma_ll_tx_enable_data_burst(hardware,ch->channel,true);
    gdma_ll_tx_enable_descriptor_burst(hardware,ch->channel,true);
    gdma_ll_tx_set_block_size_psram(hardware,ch->channel,block);
    return ESP_OK;
}
esp_err_t gdma_start(gdma_channel_handle_t ch,intptr_t descriptor) {
    if(!ch || !ch->connected || !descriptor)return ESP_ERR_INVALID_ARG;
    // Invoked by the provider's LCD ISR: no task-only CPU call or allocation.
    portENTER_CRITICAL_SAFE(&ch->lock);
    gdma_ll_tx_set_desc_addr(hardware,ch->channel,descriptor);
    gdma_ll_tx_start(hardware,ch->channel);
    portEXIT_CRITICAL_SAFE(&ch->lock);
    return ESP_OK;
}
esp_err_t gdma_disconnect(gdma_channel_handle_t ch) {
    if(!ch || xPortInIsrContext())return ESP_ERR_INVALID_ARG;
    if(!ch->connected)return ESP_OK;
    // Caller already removed its LCD IRQ. Stop only our direction/channel;
    // never reset the controller, RX sibling, clock, or another client's IRQ.
    gdma_ll_tx_stop(hardware,ch->channel);
    const TickType_t began=xTaskGetTickCount();
    for(unsigned polls=0;!gdma_ll_tx_is_fsm_idle(hardware,ch->channel);++polls) {
        if(polls>=100 || (TickType_t)(xTaskGetTickCount()-began)>=pdMS_TO_TICKS(100)+1)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    ch->connected=false;return ESP_OK; // Keep resident reservation until delete.
}
esp_err_t gdma_del_channel(gdma_channel_handle_t ch) {
    if(!ch || xPortInIsrContext())return ESP_ERR_INVALID_ARG;
    esp_err_t result=gdma_disconnect(ch);
    if(result!=ESP_OK)return result;
    if(ch->token && risc_cpu_dma_release_v3(ch->token))return ESP_FAIL;
    free(ch);return ESP_OK;
}
