#!/usr/bin/env python3
"""Actual CPU reservation implementation against a shared SDK allocator fixture.

SPI paired channels and persistent crypto TX/RX occupy this same fixture;
this tests boundary lifetimes/failure retention, not real hardware timing.
"""
from pathlib import Path
import re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'lib/elf_loader/src/esp_cpu_dma_v3.c').read_text()
source=re.sub(r'^#include "(?:freertos/[^\"]+|esp_private/gdma.h|hal/gdma_ll.h|soc/soc_caps.h)"\n','',source,flags=re.M)
preamble=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define SOC_GDMA_GROUPS 1
#define SOC_GDMA_PAIRS_PER_GROUP 5
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define GDMA_CHANNEL_DIRECTION_TX 1
#define GDMA_TRIG_PERIPH_SPI 2
#define GDMA_LL_GET_HW(n) ((void*)(uintptr_t)((n)+1))
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)p)
#define portEXIT_CRITICAL(p) ((void)p)
static TaskHandle_t task=(void*)1;
static bool isr;
static bool tx[5],rx[5],idle[5]={true,true,true,true,true};
static unsigned routes,refs,disconnects,deletes;
static bool fail_id,fail_disconnect,fail_delete,invalid_id;
static int channels[5],attached[5]={-1,-1,-1,-1,-1};
typedef int *gdma_channel_handle_t;
typedef struct {int direction;}gdma_channel_alloc_config_t;
typedef struct {int periph,instance_id;}gdma_trigger_t;
TaskHandle_t xTaskGetCurrentTaskHandle(void){return task;}
int xPortInIsrContext(void){return isr;}
bool gdma_ll_tx_is_fsm_idle(void *hw,int ch){assert(hw==(void*)1 && ch>=0 && ch<5);return idle[ch];}
int gdma_new_channel(const gdma_channel_alloc_config_t*c,gdma_channel_handle_t*out){
 assert(c->direction==GDMA_CHANNEL_DIRECTION_TX);
 for(int i=0;i<5;++i)if(!tx[i]){tx[i]=true;channels[i]=i;*out=&channels[i];++refs;return 0;}
 return 1;
}
int gdma_get_channel_id(gdma_channel_handle_t h,int*out){if(fail_id)return 1;*out=invalid_id?99:*h;return 0;}
int gdma_connect(gdma_channel_handle_t h,gdma_trigger_t t){
 assert(tx[*h] && attached[*h]==-1);
 if(routes&(1u<<t.instance_id))return 1;
 routes|=1u<<t.instance_id;attached[*h]=t.instance_id;return 0;
}
int gdma_disconnect(gdma_channel_handle_t h){
 ++disconnects;if(fail_disconnect)return 1;
 assert(attached[*h]>=0);routes&=~(1u<<attached[*h]);attached[*h]=-1;return 0;
}
int gdma_del_channel(gdma_channel_handle_t h){
 ++deletes;if(fail_delete)return 1;
 assert(tx[*h] && attached[*h]==-1);tx[*h]=false;--refs;return 0;
}
'''
main=r'''
int main(void){
 // SPI owns channel0 pair; crypto retains channel1 pair, exactly one SDK.
 tx[0]=rx[0]=tx[1]=rx[1]=true;refs=4;routes=(1u<<0)|(1u<<6);
 risc_cpu_dma_v3 a,b;uint32_t ch;
 assert(risc_cpu_dma_reserve_tx_v3(5,&a,&ch)==0 && ch==2 && refs==5 && !rx[2]);
 assert(risc_cpu_dma_reserve_tx_v3(5,&b,&ch)==RISC_CPU_DMA_PLATFORM && !b && refs==5);
 // Unrelated SDK client teardown cannot gate the controller while held.
 tx[0]=rx[0]=false;refs-=2;assert(refs==3 && tx[2]);
 task=(void*)2;assert(risc_cpu_dma_release_v3(a)==RISC_CPU_DMA_CONTEXT);task=(void*)1;
 idle[2]=false;unsigned before=deletes;
 assert(risc_cpu_dma_release_v3(a)==RISC_CPU_DMA_BUSY && deletes==before && refs==3);
 idle[2]=true;fail_disconnect=true;
 assert(risc_cpu_dma_release_v3(a)==RISC_CPU_DMA_PLATFORM && tx[2] && routes&(1u<<5));
 fail_disconnect=false;fail_delete=true;
 assert(risc_cpu_dma_release_v3(a)==RISC_CPU_DMA_PLATFORM && tx[2] && !(routes&(1u<<5)));
 before=disconnects;fail_delete=false;
 assert(!risc_cpu_dma_release_v3(a) && disconnects==before && refs==2 && rx[1] && tx[1]);
 assert(risc_cpu_dma_release_v3(a)==RISC_CPU_DMA_STALE);
 assert(!risc_cpu_dma_reserve_tx_v3(5,&b,&ch) && a!=b && ch==0);
 assert(risc_cpu_dma_release_v3(a)==RISC_CPU_DMA_STALE && tx[0]);
 assert(!risc_cpu_dma_release_v3(b));
 // Partial failures retain a retry token; invalid SDK IDs never touch MMIO.
 fail_id=fail_delete=true;
 assert(risc_cpu_dma_reserve_tx_v3(5,&a,&ch)==RISC_CPU_DMA_PLATFORM && a && ch==UINT32_MAX);
 fail_id=fail_delete=false;assert(!risc_cpu_dma_release_v3(a));
 invalid_id=true;assert(risc_cpu_dma_reserve_tx_v3(5,&a,&ch)==RISC_CPU_DMA_PLATFORM && !a);invalid_id=false;
 isr=true;assert(risc_cpu_dma_reserve_tx_v3(5,&a,&ch)==RISC_CPU_DMA_CONTEXT && !a);isr=false;
 assert(risc_cpu_dma_reserve_tx_v3(8,&a,&ch)==RISC_CPU_DMA_INVALID);
 // Exhaust the real shared direction inventory, no reservation of RX siblings.
 risc_cpu_dma_v3 held[4];unsigned route[]={1,2,3,4};
 for(unsigned i=0;i<4;++i)assert(!risc_cpu_dma_reserve_tx_v3(route[i],&held[i],&ch));
 assert(risc_cpu_dma_reserve_tx_v3(5,&a,&ch)==RISC_CPU_DMA_PLATFORM && !a);
 for(unsigned i=0;i<4;++i)assert(!risc_cpu_dma_release_v3(held[i]));
 assert(refs==2 && tx[1] && rx[1]);
 puts("CPU DMA shared allocation, stale/foreign handles, busy/partial release: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(preamble+source+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'sdk/driver'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
