/* Real HID/XInput interruption and resume at the marsh waiting awning. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
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
 memset(&ht,0,sizeof(ht));ht.level=4;ht_select_level(4);ht_spawn(true);
 ht.x=330*256;ht.y=ht_surface_at(&ht,0,330)*256;ht.grounded=true;
 ht_ferry=(ht_ferry_state){0};
 ht_waiting=(ht_waiting_state){0};ht_distribution=(ht_distribution_state){0};ht_station=(ht_station_state){0};ht_stove=(ht_stove_state){0};
 ht_cutscene.active=false;ht_cutscene.finished=true;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
 ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 ht_input(1);input(0,8);
}
static void pull(unsigned n){input(0,6);ticks(n);input(0,8);}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
 assert(memory&&copy);ht_bind(memory);ht_bind_native(memory);
 for(source=0;source<2;++source){
  reset();assert(ht.traversal.boat_x==632*256 && !ht.traversal.ferry_retrieved);
  int x=ht.x,y=ht.y;uint32_t evidence=ht.evidence;ht_puzzle_state puzzle=ht.puzzle;
  action();assert(ht_ferry.active && !ht_ferry.stage);input(0,8);ticks(100);
  assert(ht.traversal.boat_x==632*256);action();input(0,8);ticks(120);assert(ht_ferry.stage==2);
  action();input(0,8);ticks(40);assert(ht_ferry.stage==3 && ht_ferry.tick==32);
  pull(40);int boat=ht.traversal.boat_x;assert(boat<632*256 && boat>398*256);
  press(source?64:256);assert(ht_ferry.paused);pull(30);assert(ht.traversal.boat_x==boat);
  press(source?64:256);input(0,8);
  press(source?128:512);assert(reading);ticks(60);assert(ht.traversal.boat_x==boat);
  input(0,8);press(source?128:512);input(0,8);assert(!reading);
  input(0,6);pad_failure=true;input(0,6);boat=ht.traversal.boat_x;ticks(60);assert(ht.traversal.boat_x==boat);
  pad_failure=false;input(0,6);ticks(30);assert(ht.traversal.boat_x==boat);input(0,8);
  cancel();assert(!ht_ferry.active);input(0,8);assert(ht.traversal.boat_x==boat);
  action();assert(ht_ferry.active && ht_ferry.stage==2);action();input(0,8);ticks(40);
  pull(500);assert(ht_ferry.stage==4 && ht.traversal.ferry_retrieved && ht.traversal.boat_x==398*256);
  assert(ht.x==x && ht.y==y && ht.evidence==evidence && !memcmp(&puzzle,&ht.puzzle,sizeof(puzzle)));
  unsigned rev=scene_revision;ticks(300);assert(rev==scene_revision);
  action();assert(!ht_ferry.active);assert(ht.traversal.boat_x==398*256);input(0,8);
  assert(!ht_ferry_begin());
  for(int i=0;i<40 && ht_traversal_near(&ht)!=HT_BOAT;++i)ht_step_controls(1,0,false,false);
  assert(ht_traversal_interact() && ht.traversal.mode==HT_BOAT);
  for(int i=0;i<500;++i)ht_step_controls(1,0,false,false);
  assert(ht.traversal.boat_x==632*256 && ht.x==ht.traversal.boat_x);
  assert(ht_traversal_interact());
  for(int i=0;i<150 && ht.x<700*256;++i)ht_step_controls(1,0,ht.grounded && ht.traversal.support==3,true);
  assert(ht.x>=700*256 && ht.grounded && !ht.deaths);
  ht_spawn(false);assert(ht.traversal.ferry_retrieved && ht.traversal.boat_x==398*256);
  reset();action();host_exit=true;input(0,8);assert(quitting);
 }
 for(int native=0;native<2;++native){
  reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;assert(ht_ferry_begin());
  for(int stage=0;stage<=4;++stage)for(int tick=0;tick<96;++tick){
   ht_ferry.stage=(uint8_t)stage;ht_ferry.tick=(uint16_t)tick;
   ht_person_pose p=ht_ferry_pose(&ht,&ht_ferry);
   for(int i=0;i<2;++i){int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;
    if(dx*dx+dy*dy>144)fprintf(stderr,"stage %d tick %d hand %d distance %d\n",stage,tick,i,dx*dx+dy*dy);
    assert(dx*dx+dy*dy<=144);
    dx=p.foot[i].x-p.hip.x;dy=p.foot[i].y-1-p.hip.y;assert(dx*dx+dy*dy<=225);
   }
  }
  ht_ferry.stage=3;ht_ferry.tick=48;ht_game frozen=ht;
  ht_ferry_study_render(&ht,&ht_ferry);int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;memcpy(copy,ht_scene,bytes);
  ht_ferry_study_render(&ht,&ht_ferry);assert(!memcmp(copy,ht_scene,bytes));assert(!memcmp(&ht,&frozen,sizeof(ht)));
 }
 reset();assert(ht_ferry_begin());ht_ferry.stage=3;ht_ferry.tick=32;ht_ferry.direction=-1;
 for(int n=0;n<312;++n){int prior=ht.traversal.boat_x;assert(ht_ferry_step(&ht_ferry));
  assert(prior-ht.traversal.boat_x==HT_FERRY_PULL_SPEED);
  if(n<311)assert(!ht.traversal.ferry_retrieved);
 }
 assert(ht_ferry.stage==4 && ht.traversal.boat_x==398*256);
 for(int n=0;n<1000;++n)assert(!ht_ferry_step(&ht_ferry));
 reset();ht_ferry_begin();ht_ferry.stage=3;ht_ferry.tick=32;ht_ferry.direction=-1;
 ht_ferry_step(&ht_ferry);int partial=ht.traversal.boat_x;ht_spawn(false);assert(ht.traversal.boat_x==partial);
 free(copy);free(memory);
 puts("Ferry: one live boat, reachable shore/rope contacts, bounded pull, partial retry, HID/XInput interruption, neutral rearm, journal/pause/exit, original boarding/rowing/landing PASS");
}
