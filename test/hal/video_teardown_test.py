"""Compile actual video teardown with failing hardware/worker operations."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'src/native/NativeVideoBridge.cpp').read_text()
shutdown=s[s.index('bool epd_video_shutdown()'):s.index('\nnamespace {\nPca9535Min')]
wait=s[s.index('bool wait_for_dma('):s.index('\nbool send_row(')]
stop=s[s.index('void video_stop()'):s.index('\nbool video_scan_stats')]
force=s[s.index('bool nativeVideoForceStop()'):s.index('\n#else',s.index('bool nativeVideoForceStop()'))]
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
using TaskHandle_t=void*;
using esp_err_t=int;
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define pdMS_TO_TICKS(x) (x)
static bool g_running=true,g_drive_pending=true,g_flip_req=true,g_dma_done=true,s_video_started=true;
static void *g_scan_task=nullptr,*g_flip_waiter=nullptr,*g_panel_io=(void*)1,*g_i80_bus=(void*)2;
static unsigned freed,ioDeletes,busDeletes;
static bool failIo,failBus;
static int64_t nowUs;
struct Expander {void safeShutdownOutputs(){}} expander;
static Expander* g_expander=&expander;
int64_t esp_timer_get_time(){return nowUs;}
void delayMicroseconds(unsigned n){nowUs+=n;}
void vTaskDelay(unsigned n){nowUs+=n*1000;}
void xTaskNotifyGive(void*){}
void configure_idle_levels(){}
int esp_lcd_panel_io_del(void*){++ioDeletes;return failIo?1:0;}
int esp_lcd_del_i80_bus(void*){++busDeletes;return failBus?1:0;}
void release_allocations(){++freed;}
'''
main=r'''
int main(){
 g_scan_task=(void*)3; assert(!nativeVideoForceStop());
 assert(!freed && !ioDeletes && !busDeletes && s_video_started);
 g_scan_task=nullptr; g_dma_done=false; nowUs=0;
 assert(!nativeVideoForceStop()); assert(nowUs>=100000 && nowUs<110000);
 assert(!freed && !ioDeletes && !busDeletes);
 g_dma_done=true; failIo=true;
 assert(!nativeVideoForceStop()); assert(!freed && ioDeletes==1 && !busDeletes);
 failIo=false; failBus=true;
 assert(!nativeVideoForceStop()); assert(!freed && !g_panel_io && g_i80_bus);
 failBus=false;
 assert(nativeVideoForceStop()); assert(freed==1 && !g_expander);
 assert(nativeVideoForceStop()); assert(freed==1);
 puts("actual video teardown: worker/DMA timeout, retained failed handles, retry, idempotence PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'test.cpp').write_text(prefix+wait+shutdown+stop+force+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
