/* Earned physical handoff through the actual quarry route and app input. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
#include "hollow_trail_route_walk.inc"
static uint32_t now,mapped;
static bool host_exit,pad_failure;
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
static bool raw_poll(void *ctx,size_t n){(void)ctx;(void)n;return !pad_failure;}
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
static void reset(void){
 memset(&ht,0,sizeof(ht));ht.level=5;ht_select_level(5);ht_spawn(true);
 ht_hoist=(ht_hoist_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
 held=previous=mapped=0;ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;
 for(unsigned i=0;i<20000 && ht.x<(HT_GOAL-15)*256;++i)walk_route_tick();
 assert(ht.level==5 && ht.puzzle.solved && ht.puzzle.opening==48 && !ht.deaths);
 assert(ht.puzzle.value[0]==2 && ht.puzzle.value[1]==1 && ht.puzzle.value[2]==3);
 ht_input(1);input(0,8);
 for(unsigned i=0;i<1000 && ht.level==5;++i){input(0,2);ticks(1);}
 assert(ht.level==6 && ht_hoist.active);ht_select_level(6);simulation_started=false;input(0,8);
 assert(!memcmp(&ht,&ht_hoist.destination,sizeof(ht)));
}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits);ht_bind(memory);ht_bind_native(memory);
 for(source=0;source<2;++source){
  reset();ht_game frozen=ht;assert(ht.x==95*256 && ht.level==6);
  ticks(100);unsigned tick=ht_hoist.tick;assert(tick>0 && tick<128);
  press(source?64:256);assert(ht_hoist.paused);ticks(60);assert(ht_hoist.tick==tick);
  press(source?64:256);assert(!ht_hoist.paused);
  press(source?128:512);assert(reading && ht_journal_index);ticks(60);assert(ht_hoist.tick==tick);
  press(source?128:512);input(0,8);assert(!reading);
  pad_failure=true;input(0,8);ticks(30);assert(ht_hoist.tick==tick);
  pad_failure=false;input(0,8);action();assert(ht_hoist.active);ticks(800);
  assert(ht_hoist.tick==HT_HOIST_TICKS && !memcmp(&ht,&frozen,sizeof(ht)));
  unsigned rev=scene_revision;ticks(100);assert(scene_revision==rev);
  action();assert(!ht_hoist.active && ht_input_rearm);input(source?2:1,2);ticks(3);assert(ht.x==frozen.x && ht.level==6 && !ht_hoist.active && ht.evidence==frozen.evidence);
  input(0,8);input(0,2);ticks(12);assert(ht.level==6 && ht.x>frozen.x && !ht_hoist.active);
  reset();frozen=ht;ticks(50);cancel();assert(!ht_hoist.active && !memcmp(&ht,&frozen,sizeof(ht)));
  input(0,8);action();assert(!ht_hoist.active); /* No repeated transition at spawn. */
  reset();host_exit=true;input(0,8);assert(quitting);
 }
 for(int native=0;native<2;++native){
  source=0;reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned tick=0;tick<=HT_HOIST_TICKS;++tick){
   ht_hoist.tick=tick;ht_person_pose p=ht_hoist_pose(&ht_hoist);
   for(int i=0;i<2;++i){int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;
    if(dx*dx+dy*dy>144)fprintf(stderr,"hand tick=%u i=%d dx=%d dy=%d\n",tick,i,dx,dy);
    assert(dx*dx+dy*dy<=144);dx=p.foot[i].x-p.hip.x;dy=p.foot[i].y-1-p.hip.y;
    if(dx*dx+dy*dy>225)fprintf(stderr,"foot tick=%u i=%d dx=%d dy=%d\n",tick,i,dx,dy);
    assert(dx*dx+dy*dy<=225);
    if(tick<544)assert(p.foot[i].y==ht_hoist_floor(&ht_hoist,p.foot[i].x));
    if(tick>=96 && tick<512){ht_joint grip=ht_hoist_grip(&ht_hoist,i);assert(p.hand[i].x==grip.x && p.hand[i].y==grip.y);}
   }
  }
  ht_hoist.tick=320;ht_hoist_render(&ht_hoist);int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
  memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,120);ht_hoist_render(&ht_hoist);assert(!memcmp(copy,ht_scene,bytes));
  assert(!memcmp(&ht,&frozen,sizeof(ht)));
  ht_game bad=ht_hoist.source;bad.puzzle.solved=false;assert(!ht_hoist_earned(&bad,&ht));
  bad=ht_hoist.source;bad.level=4;assert(!ht_hoist_earned(&bad,&ht));
 }
 free(bits);free(copy);free(memory);
 puts("Hoist: actual balance route and earned handoff, shared floor/cord reach, immutable destination, both rasters, HID/XInput pause/journal/fault/cancel/retry/neutral recovery and host exit PASS");
}
