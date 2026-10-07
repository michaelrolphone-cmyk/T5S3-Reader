#!/usr/bin/env python3
"""Actual provider TX helper: bounded stop and CPU reservation lifetime."""
from pathlib import Path
import re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'Drivers/display_epd_video/dma.c').read_text()
s=re.sub(r'^#include "(?:esp_private/gdma.h|hal/gdma_ll.h|freertos/[^\"]+)"\n','',s,flags=re.M)
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef int esp_err_t,portMUX_TYPE,gdma_dev_t;
typedef unsigned TickType_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define GDMA_CHANNEL_DIRECTION_TX 1
#define GDMA_TRIG_PERIPH_LCD 8
#define SOC_GDMA_TRIG_PERIPH_LCD0 5
#define GDMA_LL_EXT_MEM_BK_SIZE_16B 0
#define GDMA_LL_EXT_MEM_BK_SIZE_32B 1
#define GDMA_LL_EXT_MEM_BK_SIZE_64B 2
#define GDMA_LL_GET_HW(n) ((gdma_dev_t*)1)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL_SAFE(p) ((void)p)
#define portEXIT_CRITICAL_SAFE(p) ((void)p)
#define pdMS_TO_TICKS(n) (n)
typedef struct gdma_channel_t *gdma_channel_handle_t;
typedef struct {int direction;void *sibling_chan;struct {bool reserve_sibling;}flags;}gdma_channel_alloc_config_t;
typedef struct {int periph,instance_id;}gdma_trigger_t;
typedef struct {bool owner_check,auto_update_desc;}gdma_strategy_config_t;
typedef struct {size_t sram_trans_align,psram_trans_align;}gdma_transfer_ability_t;
static bool isr,idle=true,release_fail,reserve_fail;
static unsigned tick,yields,stops,releases,frees,starts;
int xPortInIsrContext(void){return isr;}
unsigned xTaskGetTickCount(void){return tick;}
void vTaskDelay(unsigned n){assert(n==1);++tick;++yields;}
void gdma_ll_tx_stop(gdma_dev_t*h,unsigned ch){assert(h==(void*)1 && ch==3);++stops;}
bool gdma_ll_tx_is_fsm_idle(gdma_dev_t*h,unsigned ch){assert(h==(void*)1 && ch==3);return idle;}
void gdma_ll_tx_set_desc_addr(gdma_dev_t*h,unsigned ch,intptr_t d){assert(h==(void*)1 && ch==3 && d);}
void gdma_ll_tx_start(gdma_dev_t*h,unsigned ch){assert(h==(void*)1 && ch==3);++starts;}
#define LL(name,type) void name(gdma_dev_t*h,unsigned ch,type v){assert(h==(void*)1 && ch==3);(void)v;}
LL(gdma_ll_tx_enable_owner_check,bool)
LL(gdma_ll_tx_enable_auto_write_back,bool)
LL(gdma_ll_tx_enable_data_burst,bool)
LL(gdma_ll_tx_enable_descriptor_burst,bool)
LL(gdma_ll_tx_set_block_size_psram,int)
int risc_cpu_dma_reserve_tx_v3(uint32_t route,uint64_t*t,uint32_t*c){assert(route==5);*t=42;*c=reserve_fail?UINT32_MAX:3;return reserve_fail;}
int risc_cpu_dma_release_v3(uint64_t t){assert(t==42);++releases;return release_fail;}
void counted_free(void *p){++frees;free(p);}
#define free counted_free
'''
main=r'''
int main(void){
 gdma_channel_alloc_config_t config={.direction=GDMA_CHANNEL_DIRECTION_TX};
 gdma_channel_handle_t c;
 assert(!gdma_new_channel(&config,&c));
 gdma_trigger_t route={GDMA_TRIG_PERIPH_LCD,SOC_GDMA_TRIG_PERIPH_LCD0};
 assert(!gdma_connect(c,route));
 isr=true;assert(!gdma_start(c,128) && starts==1);
 assert(gdma_del_channel(c)==ESP_ERR_INVALID_ARG && !releases);isr=false;
 idle=false;assert(gdma_del_channel(c)==ESP_ERR_TIMEOUT && !releases && !frees);
 assert(yields==100 && stops==1); // both elapsed-time and work bound
 idle=true;release_fail=true;assert(gdma_del_channel(c)==ESP_FAIL && releases==1 && !frees);
 assert(gdma_start(c,128)==ESP_ERR_INVALID_ARG); // stopped but still reserved
 release_fail=false;assert(!gdma_del_channel(c) && releases==2 && frees==1);
 reserve_fail=true;assert(!gdma_new_channel(&config,&c));
 assert(gdma_connect(c,route)==ESP_FAIL && c->token==42);
 unsigned before=stops;
 assert(!gdma_del_channel(c) && stops==before && releases==3 && frees==2);
 puts("External DMA bounded TX stop, ISR admission, partial reservation retention: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+s+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'sdk/driver'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
