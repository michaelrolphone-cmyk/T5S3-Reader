#!/usr/bin/env python3
"""Run the actual staged Reader shutdown against worker and bus failures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'dist/experimental/display-epd-video/quality-source/lgfx/v1/platforms/esp32/Panel_EPD.cpp').read_text()
method=s[s.index('  bool Panel_EPD::shutdown('):s.index('  color_depth_t Panel_EPD::setColorDepth')]
prefix=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
static unsigned joins,releases,queues,frees,busReleases;
static bool joinOkay,releaseOkay,busOkay;
int risc_cpu_worker_join_v2(uint64_t token,uint32_t timeout){assert(token==42&&timeout==2000);++joins;return joinOkay?0:1;}
int risc_cpu_worker_release_v2(uint64_t token){assert(token==42);++releases;return releaseOkay?0:1;}
void vQueueDelete(void*p){assert(p);++queues;}
void heap_caps_free(void*p){assert(p);++frees;}
struct Bus{void release(){++busReleases;}bool released(){return busOkay;}} bus;
struct Panel_EPD {
 std::atomic<bool>_stop_requested{false};uint64_t _task_update_handle=42;
 Bus*_bus=&bus;
 void*_update_queue_handle=(void*)1,*_buf=(void*)2,*_step_framebuf=(void*)3,*_dma_bufs[2]={(void*)4,(void*)5},*_lut_2pixel=(void*)6;
 bool _display_busy=true;
 Bus*getBusEPD(){return _bus;}
 bool shutdown(uint32_t timeout_ms=2000);
};
'''
main=r'''
int main(){
 Panel_EPD panel;assert(!panel.shutdown());assert(panel._stop_requested&&joins==1&&!releases&&!busReleases&&!queues&&!frees);
 joinOkay=true;assert(!panel.shutdown());assert(releases==1&&panel._task_update_handle==42&&!busReleases&&!frees);
 releaseOkay=true;assert(!panel.shutdown());assert(panel._task_update_handle==0&&busReleases==1&&!queues&&!frees);
 busOkay=true;assert(panel.shutdown());assert(joins==3&&releases==2&&queues==1&&frees==5&&!panel._display_busy);
 assert(panel.shutdown());assert(queues==1&&frees==5);
 puts("Actual Reader shutdown: worker join/release and bus failure retain queues/history/DMA; retry/idempotence PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(prefix+method+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=10)
