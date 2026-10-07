/* Earned vent response through the actual glasshouse route and app controls. */
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
 memset(&ht,0,sizeof(ht));ht.level=6;ht_select_level(6);ht_spawn(true);
 ht_partition=(ht_partition_state){0};ht_hoist=(ht_hoist_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;for(unsigned i=0;i<20000 && ht.x<2840*256;++i)walk_route_tick();
 assert(ht.level==6 && !ht.puzzle.stage && !ht.deaths);ht_input(1);input(0,8);
}
static void at(int station){
 int target=HT_PUZZLE_FIRST+station*HT_PUZZLE_SPACING;
 for(unsigned i=0;i<3000 && (abs(ht.x/256-target)>3 || !ht.grounded);++i){
  input(0,abs(ht.x/256-target)<=3?8:ht.x/256<target?2:6);ticks(1);
 }
 input(0,8);assert(ht_puzzle_near(&ht)==station && ht.grounded);
}
static void earn(void){
 at(0);action();at(1);action();at(2);action();action();action();
 assert(ht_mirror_receiver(&ht.puzzle)==1 && !ht.puzzle.stage);
 at(3);action();assert(ht_partition.active && ht.puzzle.stage && !ht.puzzle.solved);input(0,8);
}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
 uint8_t *bits=malloc(HT_NATIVE_PIXELS/8),*base=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&base);ht_bind(memory);ht_bind_native(memory);
 for(source=0;source<2;++source){
  reset();at(3);action();assert(!ht_partition.active && !ht.puzzle.stage);
  earn();ht_game frozen=ht;ticks(100);action();assert(ht_partition.active);input(0,8);
  press(source?64:256);assert(ht_partition.paused);unsigned tick=ht_partition.tick;ticks(60);assert(ht_partition.tick==tick);press(source?64:256);
  press(source?128:512);assert(reading && ht_partition.active);ticks(60);assert(ht_partition.tick==tick);
  press(source?128:512);input(0,8);assert(!reading);
  pad_failure=true;input(0,8);ticks(30);assert(ht_partition.tick==tick);
  pad_failure=false;input(0,8);ticks(400);assert(ht_partition.tick==HT_PARTITION_TICKS);
  unsigned rev=scene_revision;ticks(60);assert(scene_revision==rev && !memcmp(&ht,&frozen,sizeof(ht)));
  action();input(0,8);assert(!ht_partition.active);action();assert(!ht_partition.active && !ht.puzzle.solved);
  at(2);action();action();action();assert(ht_mirror_receiver(&ht.puzzle)==2);
  at(3);action();assert(ht.puzzle.solved && !ht_partition.active);input(0,8);ticks(64);assert(ht.puzzle.opening==48);
  reset();earn();cancel();assert(!ht_partition.active && ht.puzzle.stage);input(0,8);action();assert(!ht_partition.active);
  reset();earn();host_exit=true;input(0,8);assert(quitting);
 }
 for(int native=0;native<2;++native){
  source=0;reset();earn();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  ht_partition_state state={true,false,HT_PARTITION_TICKS};ht_partition_study_render(&frozen,&state);
  int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;memcpy(copy,ht_scene,bytes);
  for(int i=0;i<10000;++i)assert(!ht_partition_step(&state));
  ht_partition_study_render(&frozen,&state);assert(!memcmp(copy,ht_scene,bytes) && !memcmp(&ht,&frozen,sizeof(ht)));
  ht_native_active=native!=0;ht_native_foreground_half_y=false;ht_world_scale=256;ht_scene=native?ht_native_a:ht_scene_low;
  ht_game v=frozen;v.camera=(HT_GOAL-330)*256;v.partition_view_active=true;v.partition_view_tick=112;
  memset(ht_scene,160,bytes);ht_partition_response(&v,190);ht_pack_mono(base,120);
  v.partition_view_tick=152;memset(ht_scene,160,bytes);ht_partition_response(&v,190);ht_pack_mono(bits,120);
  unsigned changed=0;for(int i=0;i<HT_NATIVE_PIXELS/8;++i)changed+=base[i]!=bits[i];assert(changed>10);
  v.partition_view_tick=0;memset(ht_scene,160,bytes);ht_partition_pane(&v,190);ht_pack_mono(base,120);
  v.partition_view_tick=HT_PARTITION_TICKS;memset(ht_scene,160,bytes);ht_partition_pane(&v,190);ht_pack_mono(bits,120);
  changed=0;for(int i=0;i<HT_NATIVE_PIXELS/8;++i)changed+=base[i]!=bits[i];assert(changed>100);
  ht_game bad=frozen;bad.level=7;assert(!ht_partition_earned(&bad,&frozen));
 }
 free(base);free(bits);free(copy);free(memory);
 puts("Glass partition: actual route/mirror solution, earned vent, leaf-only and pane-only mono changes, exact frozen state, HID/XInput pause/journal/fault/cancel/retry/exit and unchanged lower-latch opening PASS");
}
