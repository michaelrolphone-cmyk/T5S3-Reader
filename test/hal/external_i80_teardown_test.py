#!/usr/bin/env python3
"""Exercise the actual staged i80 teardown with failing IRQ/DMA operations."""
from pathlib import Path
import subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from prepare_t5_i80_source import prepare
source=prepare((ROOT/'dist/idf-display-source/v4.4.7/components/esp_lcd/src/esp_lcd_panel_io_i80.c').read_text())
start=source.index('esp_err_t esp_lcd_del_i80_bus(')
end=source.index('\nesp_err_t esp_lcd_new_panel_io_i80(',start)
function=source[start:end]
preamble=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_RETURN_ON_FALSE(c,e,...) do {if(!(c))return e;}while(0)
#define ESP_RETURN_ON_ERROR(c,...) do {int e=(c);if(e)return e;}while(0)
#define LIST_EMPTY(p) (!*(p))
#define LCD_LL_EVENT_TRANS_DONE 1
#define LCD_COM_DEVICE_TYPE_I80 1
typedef struct {int device_list,irq_core,bus_id;bool closing,dma_disconnected;
 struct {void*dev;}hal;void*intr,*dma_chan,*format_buffer;} bus_t;
typedef bus_t* esp_lcd_i80_bus_handle_t;
static struct {struct {int module;}buses[1];}lcd_periph_signals;
static bool isr;static int core,disable_error,irq_error,disconnect_error,dma_error;
static unsigned disabled,irq_frees,stopped,disconnects,dma_frees,removed,power_off,frees;
int xPortInIsrContext(void){return isr;}
int xPortGetCoreID(void){return core;}
void lcd_ll_enable_interrupt(void*d,int e,bool on){(void)d;(void)e;assert(!on);++disabled;}
int esp_intr_disable(void*p){assert(p);return disable_error;}
int esp_intr_free(void*p){assert(p);++irq_frees;return irq_error;}
void lcd_ll_stop(void*p){(void)p;++stopped;}
int gdma_disconnect(void*p){assert(p);++disconnects;return disconnect_error;}
int gdma_del_channel(void*p){assert(p);++dma_frees;return dma_error;}
void lcd_com_remove_device(int t,int n){(void)t;(void)n;++removed;}
void periph_module_disable(int n){(void)n;++power_off;}
void retained_free(void*p){assert(p);++frees;}
#define free retained_free
'''
main=r'''
int main(void){
 bus_t b={.intr=(void*)1,.dma_chan=(void*)2,.format_buffer=(void*)3};
 assert(esp_lcd_del_i80_bus(NULL)==ESP_ERR_INVALID_ARG);
 b.device_list=1;assert(esp_lcd_del_i80_bus(&b)==ESP_ERR_INVALID_STATE);b.device_list=0;
 core=1;assert(esp_lcd_del_i80_bus(&b)==ESP_ERR_INVALID_STATE);core=0;
 isr=true;assert(esp_lcd_del_i80_bus(&b)==ESP_ERR_INVALID_STATE);isr=false;
 assert(!disabled && !frees);
 disable_error=7;assert(esp_lcd_del_i80_bus(&b)==7);
 assert(b.closing && b.intr && b.dma_chan && !irq_frees && !frees);
 disable_error=0;irq_error=8;assert(esp_lcd_del_i80_bus(&b)==8);
 assert(b.intr && !stopped && !frees);
 irq_error=0;disconnect_error=9;assert(esp_lcd_del_i80_bus(&b)==9);
 assert(!b.intr && b.dma_chan && !b.dma_disconnected && !dma_frees && !frees);
 disconnect_error=0;dma_error=10;assert(esp_lcd_del_i80_bus(&b)==10);
 assert(b.dma_disconnected && b.dma_chan && !removed && !power_off && !frees);
 unsigned prior=disconnects;dma_error=0;assert(esp_lcd_del_i80_bus(&b)==ESP_OK);
 assert(disconnects==prior && !b.dma_chan && removed==1 && power_off==1 && frees==2);
 puts("Actual external i80 teardown: context, IRQ/DMA failure retention and retry PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 path=Path(d);(path/'test.c').write_text(preamble+function+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True,timeout=10)
