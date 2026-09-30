#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../Apps/springboard.c"
static uint32_t clock_ms;
static t5_app_contact_t contact;
static unsigned submits,allocations,stops;
static bool ready=true, fail_copy=false;
static uint8_t back[960*540/4];
static int32_t test_width(void){return 540;}
static int32_t test_height(void){return 960;}
static void nop(void){}
static void test_text(int32_t x,int32_t y,const char* t){(void)x;(void)y;(void)t;}
static void test_rect(int32_t x,int32_t y,int32_t w,int32_t h,bool b){(void)x;(void)y;(void)w;(void)h;(void)b;}
static void test_tone(int32_t x,int32_t y,int32_t w,int32_t h,int32_t r,uint8_t t){(void)x;(void)y;(void)w;(void)h;(void)r;(void)t;}
static void test_label(int32_t x,int32_t y,int32_t w,const char* t){(void)x;(void)y;(void)w;(void)t;}
static bool test_icon(int32_t x,int32_t y,const char* t,uint8_t s,bool b){(void)x;(void)y;(void)t;(void)s;(void)b;return true;}
static bool test_get(uint32_t i,t5_app_manifest_t* out){(void)i;memset(out,0,sizeof(*out));out->compatible=true;return true;}
static bool test_contact(t5_app_contact_t* out){*out=contact;return true;}
static uint32_t test_clock(void){return clock_ms;}
static void* test_alloc(size_t size){++allocations;return malloc(size);}
static void test_free(void* p){--allocations;free(p);}
static bool test_copy(uint8_t* dest,size_t capacity,t5_app_frame_t* info){
 *info=(t5_app_frame_t){960,540,240,0,0};
 if(fail_copy)return false;
 if(dest){assert(capacity==sizeof(back));memset(dest,(int)current_page(),capacity);}
 return true;
}
static bool test_start(t5_video_surface_v1* s,uint8_t f){*s=(t5_video_surface_v1){960,540,240,f,T5_VIDEO_FLAG_ONE_IS_BLACK};return true;}
static uint8_t* test_back(size_t* size){*size=sizeof(back);return back;}
static bool test_ready(void){return ready;}
static bool test_submit(uint16_t y,uint16_t h){(void)y;(void)h;++submits;return true;}
static void test_stop(void){++stops;}
static const t5_video_api_v1 test_video={.struct_size=sizeof(test_video),.start_format=test_start,.backbuffer=test_back,.can_submit=test_ready,.submit=test_submit,.stop=test_stop};
static const t5_app_api_v1 test_api={.struct_size=sizeof(test_api),.screen_width=test_width,.screen_height=test_height,
 .clear=nop,.draw_text=test_text,.fill_rect=test_rect,.draw_label=test_label,.draw_icon=test_icon,.fill_rounded_rect_tone=test_tone,
 .installed_apps_get=test_get,.millis=test_clock,.psram_alloc=test_alloc,.psram_free=test_free,.copy_ui_frame=test_copy,.touch_contact=test_contact};
const t5_app_api_v1* t5_app_get_api(uint32_t v){(void)v;return &test_api;}
const t5_storage_api_v1* t5_storage_get_api(uint32_t v){(void)v;return NULL;}
const t5_video_api_v1* t5_video_get_api(uint32_t v){(void)v;return &test_video;}
static bool step(uint32_t now,bool down,int x,int y,const t5_app_swipe_t* swipe){
 clock_ms=now;contact=(t5_app_contact_t){down,x,y};t5_app_input_t input={0};
 bool consumed=sv_input(&input,swipe!=NULL,swipe);sv_present();return consumed;
}
int main(void){
 api=&test_api;count=35;selected=0;layout();
 assert(app_hardware_takeover()==(T5_HARDWARE_TAKEOVER_DISPLAY|T5_HARDWARE_TAKEOVER_UI_VIDEO));
 assert(sv_open());sv_refresh(NULL);sv_present();assert(allocations==3 && submits==1);
 assert(!step(20,true,400,300,NULL));
 assert(step(60,true,250,300,NULL));assert(sv_offset==-150 && current_page()==0 && submits==2);
 t5_app_swipe_t swipe={400,300,200,300};
 assert(step(100,false,0,0,&swipe));assert(sv_animating);
 assert(step(190,false,0,0,NULL));assert(sv_offset < -150 && sv_offset > -540);
 assert(step(280,false,0,0,NULL));assert(current_page()==1 && sv_offset==0 && !sv_animating);
 // Small drags and loss of stream continuity snap back without a page change.
 step(300,true,400,300,NULL);assert(step(340,true,360,300,NULL));
 assert(step(380,false,0,0,NULL));step(560,false,0,0,NULL);assert(current_page()==1);
 // Vertical gesture never drags the grid or launches an icon.
 step(600,true,300,300,NULL);assert(!step(640,true,302,500,NULL));
 swipe=(t5_app_swipe_t){300,300,302,500};assert(step(680,false,0,0,&swipe));assert(!sv_animating);
 // Latest frame waits for capacity; never write/submit a queued backbuffer.
 ready=false;sv_dirty=true;unsigned before=submits;step(720,false,0,0,NULL);assert(submits==before);
 ready=true;step(760,false,0,0,NULL);assert(submits==before+1);
 // Corrupt snapshot fails closed, and normal teardown frees all cache memory.
 fail_copy=true;sv_refresh(NULL);assert(sv_fatal);sv_close();assert(!allocations && stops==1);
 puts("springboard live drag, eased settle, cancellation, vertical lock, backpressure and cleanup PASS");
}
