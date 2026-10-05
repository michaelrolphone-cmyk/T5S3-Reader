/* Real HID/XInput interruption and resume of the signal-window attachment. */
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
static void reset(void) {
 memset(&ht,0,sizeof(ht));ht.level=2;ht_select_level(2);ht_spawn(true);
 ht.x=(HT_STOVE_X-18)*256;ht.y=ht_stove_floor()*256;ht.grounded=true;
 ht_stove=(ht_stove_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
 ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 ht_input(1);input(0,8);
}
int main(void) {
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*plain=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&plain);ht_bind(memory);ht_bind_native(memory);
 for(source=0;source<2;++source) {
  reset();assert(ht_stove_near(&ht));action();
  assert(ht_stove.active && !reading && ht_evidence_found(&ht,7));
  ht_game frozen=ht;
  /* Incoming sequence progresses under ordinary frequent input polling. */
  for(int n=0;n<220 && !ht_stove.stage;++n){input(0,8);ticks(1);}
  assert(ht_stove.stage==1 && !memcmp(&ht,&frozen,sizeof(ht)));
  action();assert(ht_stove.stage==2);
  unsigned start=ht_stove.tick;for(int n=0;n<20;++n){input(source?2:1,8);ticks(1);}
  assert(ht_stove.stage==2 && ht_stove.tick>start); /* held A cannot restart */
  pad_failure=true;input(0,8);unsigned fault_tick=ht_stove.tick;
  ticks(30);assert(ht_stove.tick==fault_tick && !memcmp(&ht,&frozen,sizeof(ht)));
  pad_failure=false;input(0,8);input(0,8);
  press(source?64:256);assert(ht_stove.paused);unsigned t=ht_stove.tick;
  ticks(100);assert(ht_stove.tick==t && !memcmp(&ht,&frozen,sizeof(ht)));
  press(source?64:256);assert(!ht_stove.paused);
  press(source?128:512);assert(reading && ht_stove.active);t=ht_stove.tick;
  ticks(100);assert(ht_stove.tick==t);
  input(0,8);press(source?128:512);assert(!reading && ht_stove.active);
  input(0,8);ticks(500);assert(ht_stove.stage==3 && ht_stove.tick==320);
  unsigned age=99;assert(!ht_stove_pulse(&ht_stove,&age));
  assert(!memcmp(&ht,&frozen,sizeof(ht)));
  action();assert(reading && ht_stove.active);input(0,8);press(source?128:512);input(0,8);
  cancel();assert(!ht_stove.active && !reading && ht_input_rearm);
  input(0,8);action();assert(ht_stove.active && ht_stove.stage==0);
  host_exit=true;input(0,8);assert(quitting);host_exit=false;
 }
 for(int native=0;native<2;++native) {
  reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
  ht_stove_begin();ht_game frozen=ht;ht_stove_state state=ht_stove;
  ht_stove_study_render(&frozen,&state);int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
  memcpy(copy,ht_scene,bytes);ht_stove_study_render(&frozen,&state);assert(!memcmp(copy,ht_scene,bytes));
  assert(!memcmp(&ht,&frozen,sizeof(ht)) && !memcmp(&state,&ht_stove,sizeof(state)));
  state.tick=42;unsigned age;assert(ht_stove_pulse(&state,&age)==1 && age==2);
  ht_stove_study_render(&frozen,&state);assert(memcmp(copy,ht_scene,bytes));
  state.tick=80;assert(ht_stove_pulse(&state,&age)==2);
  state.tick=118;assert(ht_stove_pulse(&state,&age)==3);
  state.tick=155;assert(!ht_stove_pulse(&state,&age));
  /* Absent-coat edges remain visible after the production mono packer. */
  ht_world_scale=384;ht_native_active=native!=0;
  ht_scene=native?ht_native_a:ht_scene_low;
  memset(ht_scene,146,bytes);ht_pack_mono(plain,120);
  ht_stove_coat_marks(228,191);ht_pack_mono(bits,120);
  unsigned changed=0;for(int i=0;i<HT_NATIVE_PIXELS/8;++i)changed+=bits[i]!=plain[i];
  assert(changed>40);
 }
 assert(ht_evidence_platform(2,1)==5 && ht_evidence_x(2,1)==HT_STOVE_X);
 reset();ht.x=1665*256;ht.y=220*256;assert(!ht_stove_near(&ht));
 free(bits);free(plain);free(copy);free(memory);
 puts("Stove: physical post-culvert placement, HID/XInput stages, no repeated held reply, pause/journal/resume, cancel/replay/exit, knock timing and immutable rasters PASS");
}
