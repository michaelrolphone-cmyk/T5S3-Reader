"""Exercise the actual shared video-start code with bounded hardware doubles."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/native/NativeVideoBridge.cpp').read_text()
start = source[source.index('bool video_start_format('):source.index('uint8_t *video_backbuffer(')]
prefix = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <T5VideoApi.h>
#include "NativeVideoBootScrub.h"
#include "NativeVideoGray.h"
#include "NativeVideoMono.h"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
namespace t5s3_epd {constexpr unsigned kActiveWidth=960,kActiveHeight=540,kPca9535Address=0x20;}
constexpr size_t kGrayRowBytes=240,kSourceRowBytes=120,kGrayBufferBytes=129600,kBackbufferBytes=64800;
constexpr size_t kStateRowBytes=480,kStateBufferBytes=259200,kGrayStateBytes=518400;
static bool s_video_started=false,g_running=false,borrowed=true,failAlloc=false,stall=false,failPower=false;
static bool failTeardown=false;
static unsigned starts=0,shutdowns=0,now=0;
static int g_boot_scrub_step=-1;
static uint8_t g_pixel_format=0;
static uint8_t *g_boot_scrub_map=nullptr,*g_buffers[2]={},*g_state_buffer=nullptr;
static size_t g_source_row_bytes,g_backbuffer_bytes,g_state_row_bytes,g_state_buffer_bytes;
static void *g_scan_task=nullptr,*g_panel_io=nullptr,*g_i80_bus=nullptr,*g_expander=nullptr;
struct Pca9535Min {bool begin(int,int){return true;} bool configureProbeDefaults(){return true;}} s_video_expander;
static int Wire=0;
namespace Board {void beginI2C(){}}
bool native_hardware_display_is_borrowed(){return borrowed;}
unsigned millis(){return now;}
void vTaskDelay(int){++now;if(g_running&&!stall&&g_boot_scrub_step>=0&&g_boot_scrub_step<24)++g_boot_scrub_step;}
void* heap_caps_malloc(size_t n,int){return failAlloc?nullptr:malloc(n);}
void free_buffer(uint8_t*& p){free(p);p=nullptr;}
bool epd_video_init(Pca9535Min& e){
 g_expander=&e;g_buffers[0]=(uint8_t*)malloc(g_backbuffer_bytes);
 g_buffers[1]=(uint8_t*)malloc(g_backbuffer_bytes);g_state_buffer=(uint8_t*)malloc(g_state_buffer_bytes);
 memset(g_buffers[0],255,g_backbuffer_bytes);memset(g_buffers[1],255,g_backbuffer_bytes);
 memset(g_state_buffer,255,g_state_buffer_bytes);return true;
}
bool epd_video_power_on(){return !failPower;}
bool epd_video_start(){++starts;g_running=true;g_scan_task=(void*)1;return true;}
bool epd_video_shutdown(){
 ++shutdowns;g_running=false;
 if(failTeardown)return false;
 free_buffer(g_boot_scrub_map);free_buffer(g_buffers[0]);free_buffer(g_buffers[1]);free_buffer(g_state_buffer);
 g_expander=g_scan_task=g_panel_io=g_i80_bus=nullptr;g_boot_scrub_step=-1;return true;
}
'''
main = r'''
int main(){
 t5_video_surface_v1 s{};
 borrowed=false;assert(!video_start(&s));assert(!starts&&!g_expander);borrowed=true;
 assert(!video_start_format(&s,99));assert(!starts);
 for(auto format:{T5_VIDEO_PIXEL_MONO_1BPP_MSB,T5_VIDEO_PIXEL_GRAY_2BPP_MSB}){
   const unsigned prior=starts;assert(video_start_format(&s,format));assert(starts==prior+1);
   assert(s.pixel_format==format&&s.width==960&&s.height==540);
   assert(!g_boot_scrub_map&&g_boot_scrub_step==-1);
   for(size_t i=0;i<g_backbuffer_bytes;++i)assert(g_buffers[0][i]==0&&g_buffers[1][i]==0);
   for(size_t i=0;i<g_state_buffer_bytes;++i)assert(g_state_buffer[i]==(format==2?0:0xfc));
   uint8_t row[240];memset(row,255,sizeof(row));
   if(format==2){
     assert(!nativeVideoBuildGrayRow(g_buffers[0],g_state_buffer,row,240));
     for(auto p:row)assert(p==0); // No unknown-state reset after the scrub.
     memset(g_buffers[0],0x55,240); // First light-gray target: one black pulse.
     assert(!nativeVideoBuildGrayRow(g_buffers[0],g_state_buffer,row,240));
     for(auto p:row)assert(p==0x55);
   }else{
     NativeVideoMonoTable table;table.init();assert(table.row(g_buffers[0],g_state_buffer,row,120)==0);
     for(auto p:row)assert(p==0);
   }
   assert(video_start_format(&s,format)&&starts==prior+1); // idempotent
   assert(!video_start_format(&s,format==1?2:1)&&starts==prior+1);
   epd_video_shutdown();s_video_started=false;
 }
 failAlloc=true;assert(!video_start(&s)&&!g_expander&&!s_video_started);failAlloc=false;
 failPower=true;assert(!video_start(&s)&&!g_expander&&!s_video_started);failPower=false;
 stall=true;const unsigned began=now;assert(!video_start(&s));assert(now-began>=1800&&now-began<1900);
 assert(!g_expander&&!g_boot_scrub_map&&!s_video_started);
 failTeardown=true;assert(!video_start(&s));assert(g_expander&&g_scan_task&&!s_video_started);
 const unsigned prior=starts;assert(!video_start(&s)&&starts==prior); // retained owner rejects restart
 failTeardown=false;epd_video_shutdown();stall=false;
 assert(video_start(&s));epd_video_shutdown();s_video_started=false;
 puts("shared mono/gray startup: scrub, settled state, repeat start, failure/deadline and retained-owner checks PASS");
}
'''
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    (path/'test.cpp').write_text('#include <initializer_list>\n'+prefix+start+main)
    subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'lib/NativeApps/include'),'-I'+str(ROOT/'src/native'),
                    str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True,timeout=10)
