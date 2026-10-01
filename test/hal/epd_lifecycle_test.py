"""Exercise patched, pinned upstream EPD allocation/startup/shutdown methods."""
from pathlib import Path
import sys, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from patch_m5gfx_lifecycle import patch_sources
fixture=ROOT/'test/fixtures/m5gfx-0.2.20'
h=(fixture/'Panel_EPD.hpp').read_text(); c=(fixture/'Panel_EPD.cpp').read_text()
h,c=patch_sources(h,c)
assert patch_sources(h,c)==(h,c)
try:
    patch_sources(h.replace('joined EPD lifetime v1','bad marker'),c)
    raise AssertionError('source drift accepted')
except RuntimeError:
    pass
shutdown=c[c.index('  bool Panel_EPD::shutdown('):c.index('  color_depth_t Panel_EPD::setColorDepth')]
initialize=c[c.index('  bool Panel_EPD::init_intenal('):c.index('  void Panel_EPD::beginTransaction')]
worker=c[c.index('  void Panel_EPD::task_update'):]
assert worker.index('bus->endTransaction()') < worker.index('_worker_done.store(true')
assert worker.index('_worker_done.store(true') < worker.index('vTaskDelete(nullptr)')
assert 'portMAX_DELAY' not in worker
prefix=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unordered_set>
using TickType_t=uint32_t;
using TaskHandle_t=void*;
using QueueHandle_t=void*;
using TaskFunction_t=void(*)(void*);
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define portNUM_PROCESSORS 2
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_SPIRAM 2
static uint32_t ticks;
static int allocations, failAt=-1;
static bool failQueue, failTask;
static unsigned queues;
static std::unordered_set<void*> live;
static std::atomic<bool>* completing;
uint32_t xTaskGetTickCount(){return ticks;}
void vTaskDelay(unsigned n){ticks+=n; if(completing && ticks>=5){completing->store(true);completing=nullptr;}}
int xPortGetCoreID(){return 0;}
void* heap_caps_malloc(size_t n,int){if(allocations++==failAt)return nullptr; auto p=malloc(n);assert(p);live.insert(p);return p;}
void* heap_caps_aligned_alloc(size_t,size_t n,int c){return heap_caps_malloc(n,c);}
void heap_caps_free(void* p){assert(live.erase(p)==1);free(p);}
void* xQueueCreate(int,size_t){if(failQueue)return nullptr;++queues;return (void*)1;}
void vQueueDelete(void*){assert(queues);--queues;}
int xTaskCreatePinnedToCore(TaskFunction_t,const char*,int,void*,int,void** h,int){if(failTask)return 0;*h=(void*)2;return pdPASS;}
static uint32_t lut_eraser[]={0};
static size_t lut_eraser_step=1;
enum epd_mode_t {epd_quality=1,epd_text,epd_fast,epd_fastest};
class Panel_EPD {
public:
 bool shutdown(uint32_t timeout_ms=20);
 bool init_intenal();
 static void task_update(void*){}
 struct {size_t memory_width=960,memory_height=540,panel_width=960,panel_height=540;} _cfg;
 struct {size_t lut_quality_step=1,lut_text_step=1,lut_fast_step=1,lut_fastest_step=1;
  uint32_t *lut_quality=lut_eraser,*lut_text=lut_eraser,*lut_fast=lut_eraser,*lut_fastest=lut_eraser;
  unsigned line_padding=8,task_priority=2,task_pinned_core=1;} _config_detail;
 struct update_data_t{int unused;};
 std::atomic<bool> _worker_done{true},_stop_requested{false};
 void *_task_update_handle=nullptr,*_update_queue_handle=nullptr;
 uint8_t *_buf=nullptr,*_dma_bufs[2]={},*_lut_2pixel=nullptr;
 uint16_t *_step_framebuf=nullptr;
 uint8_t _lut_offset_table[6]={},_lut_remain_table[6]={};
 bool _display_busy=false;
};
'''
main=r'''
int main(){
 for(unsigned cycle=0;cycle<100;++cycle){
   Panel_EPD p; allocations=0; ticks=0;
   assert(p.init_intenal()); assert(live.size()==5 && queues==1);
   // A busy worker prevents ANY buffer or queue being reclaimed.
   assert(!p.shutdown(2)); assert(live.size()==5 && queues==1);
   completing=&p._worker_done;
   assert(p.shutdown()); assert(live.empty() && queues==0);
   assert(p.shutdown());
 }
 for(int failure=0;failure<7;++failure){
   Panel_EPD p; allocations=0; failAt=failure;
   failQueue=failure==5; failTask=failure==6;
   assert(!p.init_intenal());
   assert(p.shutdown()); assert(live.empty() && queues==0);
 }
 puts("actual patched M5GFX: 100 teardown cycles, active worker retention, all allocation/queue/task failures PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp); (path/'test.cpp').write_text(prefix+shutdown+initialize+main)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Wno-sign-compare','-fsanitize=address,undefined',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
