/* Real HID/XInput interruption and resume of the quarry pouch interaction. */
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
 memset(&ht,0,sizeof(ht));ht.level=5;ht_select_level(5);ht_spawn(true);
 ht.x=ht_evidence_x(5,0)*256;ht.y=ht_surface_at(&ht,1,ht_evidence_x(5,0))*256;ht.grounded=true;
 ht_pouch=(ht_pouch_state){0};ht_waiting=(ht_waiting_state){0};ht_distribution=(ht_distribution_state){0};ht_station=(ht_station_state){0};ht_stove=(ht_stove_state){0};
 ht_cutscene.active=false;ht_cutscene.finished=true;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
 ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 ht_input(1);input(0,8);
}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
 uint8_t *bits=malloc(HT_NATIVE_PIXELS/8),*base=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&base);ht_bind(memory);ht_bind_native(memory);
 for(source=0;source<2;++source){
  reset();action();assert(ht_pouch.active && ht_evidence_found(&ht,15));ht_game frozen=ht;
  input(0,8);unsigned rev=scene_revision;ticks(60);assert(!ht_pouch.stage && scene_revision==rev);
  action();input(0,8);ticks(110);assert(ht_pouch.stage==2 && !ht_pouch.turn);
  input(0,2);ticks(32);input(0,8);assert(ht_pouch.turn>0 && ht_pouch.turn<64);
  unsigned at=ht_pouch.turn;rev=scene_revision;ticks(40);assert(ht_pouch.turn==at && scene_revision==rev);
  press(source?64:256);assert(ht_pouch.paused);input(0,2);ticks(60);assert(ht_pouch.turn==at);
  input(0,8);press(source?64:256);assert(!ht_pouch.paused);
  press(source?128:512);assert(reading && ht_pouch.active);ticks(60);assert(ht_pouch.turn==at);
  input(0,8);press(source?128:512);input(0,8);assert(!reading);
  input(0,2);ticks(80);input(0,8);assert(ht_pouch.turn==64);
  pad_failure=true;input(0,8);ticks(30);assert(ht_pouch.turn==64);
  pad_failure=false;input(0,8);input(0,6);ticks(80);input(0,8);assert(!ht_pouch.turn);
  action();input(0,8);ticks(60);assert(ht_pouch.stage==3);
  action();assert(ht_pouch.stage==3);input(0,8);ticks(100);assert(ht_pouch.stage==4);
  rev=scene_revision;ticks(60);assert(scene_revision==rev && ht_pouch.tick==144);
  assert(!memcmp(&ht,&frozen,sizeof(ht)));
  action();assert(reading);input(0,8);press(source?128:512);input(0,8);
  cancel();assert(!ht_pouch.active);input(0,8);action();assert(!ht_pouch.stage);
  host_exit=true;input(0,8);assert(quitting);
 }
 for(int native=0;native<2;++native){
  reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  ht_pouch_state state={true,false,0,2,0,0};
  ht_pouch_study_render(&frozen,&state);int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
  memcpy(copy,ht_scene,bytes);ht_pouch_study_render(&frozen,&state);assert(!memcmp(copy,ht_scene,bytes));
  state.turn=64;ht_pouch_study_render(&frozen,&state);assert(memcmp(copy,ht_scene,bytes));
  assert(!memcmp(&ht,&frozen,sizeof(ht)));
  ht_world_scale=256;ht_native_active=native!=0;ht_native_foreground_half_y=false;ht_scene=native?ht_native_a:ht_scene_low;
  memset(ht_scene,139,bytes);ht_pouch_stone(155,108,23,0,true);ht_pack_mono(base,120);
  memset(ht_scene,139,bytes);ht_pouch_stone(155,108,23,64,true);ht_pack_mono(bits,120);assert(memcmp(base,bits,HT_NATIVE_PIXELS/8));
  state.stage=4;state.tick=144;state.direction=1;for(int t=0;t<10000;++t)assert(!ht_pouch_step(&state));
 }
 for(int n=0;n<=64;++n){ht_joint p=ht_pouch_rot(5,6,n);assert(p.x*p.x+p.y*p.y<=65);}
 free(base);free(bits);free(copy);free(memory);
 puts("Quarry pouch: six-stone sequence, continuous physical face rotation, mono distinction, bounded return-last hold, HID/XInput interruption/retry/exit and frozen gameplay PASS");
}
