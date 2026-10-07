#!/usr/bin/env python3
"""Compile the actual dispatcher against failing engine implementations."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[2]
source=(root/'Drivers/display_epd_video/dispatch.cpp').read_text().replace('#include "ProviderEnvironment.hpp"','''#include <cstdint>
#include <cstring>
#include "RiscDisplayPowerV1.h"
#include "RiscPlatformClockV1.h"
#include "T5DisplayProviderV1.h"
extern int xPortInIsrContext();
extern int64_t esp_timer_get_time();
extern void vTaskDelay(unsigned);
extern "C" const t5_display_quality_api_v1 *display_quality_api();
extern "C" const t5_video_api_v1 *display_fast_api(uint32_t);
''')
main=r'''
#include <cassert>
#include <cstdio>
static bool isr=false,qstop=true,fstop=true,qstart=true,fstart=true;
static unsigned startsQ,startsF,stopsQ,stopsF,destroys,portableInvalidations;
extern "C" void display_output_fast_stopped(){++portableInvalidations;}
int xPortInIsrContext(){return isr;}
int64_t esp_timer_get_time(){return 0;}
void vTaskDelay(unsigned){}
static bool qs(bool){++startsQ;return qstart;}
static bool qt(){++stopsQ;return qstop;}
static bool fs(t5_video_surface_v1*,uint8_t){++startsF;return fstart;}
static bool ft(){++stopsF;return fstop;}
static const t5_display_quality_api_v1 qapi={1,sizeof(qapi),qs,nullptr,nullptr,nullptr,nullptr,qt};
static const t5_video_api_v1 fapi={1,sizeof(fapi),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,fs,nullptr,nullptr,ft};
extern "C" const t5_display_quality_api_v1*display_quality_api(){return &qapi;}
extern "C" const t5_video_api_v1*display_fast_api(uint32_t){return &fapi;}
bool display_quality_destroy(){++destroys;return true;}
static bool acquire(void*,uint64_t*){return true;}
static bool release(void*,uint64_t){return true;}
static uint64_t now(void*){return 0;}
static void sleep(void*,uint32_t){}
int main(){
 risc_display_power_api_v1 power={1,sizeof(power),nullptr,acquire,release};
 risc_platform_clock_api_v1 clock={1,sizeof(clock),nullptr,now,sleep};
 risc_provider_dependency_v1 deps[]={{"display.power",1,&power},{"platform.clock",1,&clock}};
 assert(!display_provider_start(nullptr,0));
 deps[1].capability_id="display.power";assert(!display_provider_start(deps,2));deps[1].capability_id="platform.clock";
 assert(display_provider_start(deps,2));assert(!display_provider_start(deps,2));
 isr=true;assert(!display_quality_dispatch.start(false));assert(!display_provider_quiesce());isr=false;
 assert(display_quality_dispatch.start(false));assert(startsQ==1);
 assert(!display_fast_dispatch.start_format(nullptr,1)&&!startsF);
 qstop=false;assert(!display_quality_dispatch.try_stop());assert(!display_fast_dispatch.start(nullptr));
 assert(!display_provider_quiesce()&&display_power==&power&&display_clock==&clock);
 assert(!display_quality_dispatch.start(false));qstop=true;
 assert(display_provider_quiesce()&&!display_power&&!display_clock&&destroys==1);
 assert(display_provider_start(deps,2));qstart=false;qstop=false;
 assert(!display_quality_dispatch.start(false));assert(!display_fast_dispatch.start(nullptr));
 qstop=true;assert(display_quality_dispatch.try_stop());
 assert(display_fast_dispatch.start_format(nullptr,1));assert(!display_quality_dispatch.start(false));
 fstop=false;assert(!display_fast_dispatch.try_stop());assert(!portableInvalidations);assert(!display_provider_quiesce());
 assert(display_power==&power);fstop=true;assert(display_provider_quiesce());assert(portableInvalidations==1);
 assert(display_provider_start(deps,2));fstart=false;fstop=false;
 assert(!display_fast_dispatch.start_format(nullptr,1));assert(!display_quality_dispatch.start(false));
 fstop=true;assert(display_provider_quiesce());
 puts("Actual display dispatcher: exclusive engines, dependencies, ISR rejection, failed startup/stop retention PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 path=Path(d);(path/'freertos').mkdir()
 for f in ['FreeRTOS.h','task.h']:(path/'freertos'/f).write_text('#pragma once\n')
 (path/'test.cpp').write_text(source+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(path),'-I'+str(root/'sdk/driver'),'-I'+str(root/'lib/NativeApps/include'),'-I'+str(root/'Drivers/display_epd_video'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True,timeout=10)
