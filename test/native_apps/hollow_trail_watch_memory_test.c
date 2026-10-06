/* The winter memory must be earned through actual log pagination and input. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
static uint32_t now,mapped;
static bool host_exit;
static risc_usb_gamepad_state_v1 report;
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    now+=wait;memset(out,0,sizeof(*out));out->buttons=mapped;out->exit_requested=host_exit;return true;
}
static uint32_t clock_ms(void) {return now;}
static const t5_app_api_v1 fake_app={.abi_version=1,.struct_size=sizeof(fake_app),.poll=fake_poll,.millis=clock_ms};
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return &fake_app;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
static bool raw_poll(void *ctx,size_t n){(void)ctx;(void)n;return true;}
static bool snapshot(void *ctx,risc_usb_gamepad_state_v1 *out,size_t *count) {
    (void)ctx;assert(*count);*out=report;*count=1;return true;
}
static const risc_usb_gamepad_api_v1 gamepad={.api_version=1,.struct_size=sizeof(gamepad),.poll=raw_poll,.snapshot=snapshot};
static unsigned source;
static void input(uint32_t buttons,unsigned hat) {report.buttons=buttons;report.hat=(uint8_t)hat;ht_input(1);}
static void press(uint32_t mask){input(0,8);input(mask,8);}
static void action(void){press(source?2:1);}
static void cancel(void){press(source?4:8);}
static void ticks(unsigned n){for(unsigned i=0;i<n;++i)ht_advance(now+=HT_STEP_MS);}
static void present(void){ht_journal_render();assert(ht_journal_page_ready);ht_read_submitted_revision=scene_revision;}
static void reset(void) {
 memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
 ht.x=HT_SIGNAL_LOG_X*256;ht.y=ht_land[7].top*256;ht.grounded=true;
 ht_cutscene.active=ht_cutscene.finished=false;ht_cutscene_seen=0;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
 ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=false;app=&fake_app;
 ht_input(1);input(0,8);
}
static void read_log(void) {action();assert(ht_signal_room_studying);action();assert(reading && journal_page==5);input(0,8);}
static void last_page(void) {
 for(unsigned page=0;;++page){assert(page<63);present();if(ht_journal_next==ht_journal_length)break;input(0,8);input(0,2);}
}
int main(void) {
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8);
 assert(mem&&copy&&bits);ht_bind(mem);ht_bind_native(mem);ht_reader_bitmap=bits;
 for(source=0;source<2;++source) {
  reset();assert(!ht_cutscene_watch_read(&ht));read_log();assert(ht_cutscene_watch_read(&ht));
  action();assert(ht_journal_index && !ht_cutscene.active);cancel();assert(!reading);
  read_log();last_page();ht_read_submitted_revision=scene_revision-1;
  action();assert(!ht_cutscene.active);cancel();assert(!reading);
  read_log();last_page();ht_game frozen=ht;action();
  assert(ht_cutscene.active && ht_cutscene.id==HT_CUTSCENE_WATCH && !reading && ht_input_rearm);
  assert(!memcmp(&ht,&frozen,sizeof(ht)) && !ht_cutscene_watch_read(&ht));
  for(int mode=0;mode<2;++mode)for(unsigned t=0;t<1200;t+=31) {
   ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_cutscene.tick=(uint16_t)t;
   ht_cutscene_render(&ht_cutscene);int bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
   memcpy(copy,ht_scene,bytes);memset(ht_scene,255,bytes);ht_cutscene_render(&ht_cutscene);
   assert(!memcmp(copy,ht_scene,bytes) && !memcmp(&ht,&frozen,sizeof(ht)));
   ht_pack_mono(bits,120);
  }
  for(unsigned t=144;t<560;++t) {
   ht_person_pose p=ht_watch_attending_pose(t);
   for(int i=0;i<2;++i) {
    int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;assert(dx*dx+dy*dy<=144);
    dx=p.foot[i].x-p.hip.x;dy=p.foot[i].y-p.hip.y;assert(dx*dx+dy*dy<=225);
   }
  }
  ht_cutscene.tick=0;input(0,8);
  for(unsigned guard=0;ht_cutscene.active && guard<HT_WATCH_MEMORY_TICKS+2;++guard)ticks(1);
  assert(!ht_cutscene.active && ht_cutscene.finished && ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
  input(0,8);read_log();last_page();action();assert(!ht_cutscene.active);cancel();
  /* Deliberate cancellation and device exit remain live. */
  reset();read_log();last_page();action();input(0,8);cancel();
  assert(!ht_cutscene.active && ht_input_rearm && !reading && !quitting);
  reset();read_log();last_page();action();input(0,8);press(source?128:512);
  assert(!ht_cutscene.active && reading && journal_page==5 && ht_input_rearm);
  reset();read_log();last_page();action();input(0,8);host_exit=true;ht_input(1);assert(quitting);
  reset();ht.evidence|=1u<<5;ht.x=1200*256;assert(!ht_cutscene_watch_read(&ht));
 }
 free(bits);free(copy);free(mem);
 puts("Watch memory: real HID/XInput log completion, submission/edge gates, exact frozen state, replay/cancel/exit, both rasters and limb reach PASS");
}
