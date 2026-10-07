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
 memset(&ht,0,sizeof(ht));ht.level=3;ht_select_level(3);ht_spawn(true);
 ht.x=HT_STATION_TICKET_X*256;ht.y=ht_station_floor()*256;ht.grounded=true;
 ht_station=(ht_station_state){0};ht_stove=(ht_stove_state){0};
 ht_cutscene.active=false;ht_cutscene.finished=true;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
 ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 ht_input(1);input(0,8);
}
int main(void) {
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*base=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&base);ht_bind(memory);ht_bind_native(memory);
 for(source=0;source<2;++source) {
  reset();action();assert(ht_station.active && ht_evidence_found(&ht,9) && !reading);
  ht_game frozen=ht;input(0,8);unsigned revision=scene_revision;ticks(40);
  assert(!ht_station.crease && scene_revision==revision);
  action();ticks(40);assert(ht_station.crease==24);input(0,8);ticks(40);assert(!ht_station.crease);
  assert(!memcmp(&ht,&frozen,sizeof(ht)));
  input(0,2);input(0,8);assert(ht_station.focus==1);
  input(0,2);input(0,8);assert(ht_station.focus==2);
  input(0,2);input(0,8);assert(ht_station.focus==3);
  input(0,2);input(0,8);assert(ht_station.focus==0);
  action();ticks(12);press(source?64:256);assert(ht_station.paused);
  unsigned t=ht_station.crease;ticks(80);assert(ht_station.crease==t);
  input(0,2);input(0,8);assert(ht_station.focus==0);
  press(source?64:256);assert(!ht_station.paused);
  press(source?128:512);assert(reading && ht_station.active);t=ht_station.crease;
  ticks(90);assert(ht_station.crease==t);
  input(0,8);press(source?128:512);assert(!reading && ht_station.active);
  input(0,8);ticks(40);assert(!ht_station.crease);
  pad_failure=true;input(0,8);t=ht_station.crease;ticks(60);assert(ht_station.crease==t);
  pad_failure=false;input(0,8);input(0,8);
  assert(!memcmp(&ht,&frozen,sizeof(ht)));
  cancel();assert(!ht_station.active && ht_input_rearm);
  input(0,8);action();assert(ht_station.active && !ht_station.focus && !ht_station.crease);
  host_exit=true;input(0,8);assert(quitting);
 }
 for(int native=0;native<2;++native) {
  reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
  ht_station_begin();ht_game frozen=ht;
  for(int focus=0;focus<4;++focus) {
   ht_station_state state=ht_station;state.focus=(uint8_t)focus;
   int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
   ht_station_study_render(&frozen,&state);memcpy(copy,ht_scene,bytes);
   ht_station_study_render(&frozen,&state);assert(!memcmp(copy,ht_scene,bytes));
   assert(!memcmp(&ht,&frozen,sizeof(ht)));
   if(!focus) {
    ht_pack_mono(base,120);state.crease=24;state.pressing=true;
    ht_station_study_render(&frozen,&state);assert(memcmp(copy,ht_scene,bytes));ht_pack_mono(bits,120);
    /* This crease-only crop is left of every finger/forearm and far above
     * narration. The change must survive mono independently of the overlay. */
    unsigned changed=0;
    for(int y=238;y<246;++y)for(int x=336;x<496;++x)
     changed+=((base[y*120+x/8]^bits[y*120+x/8])&(1u<<(7-(x&7))))!=0;
    printf("Station crease-only mono: native=%d changed=%u\n",native,changed);assert(changed>80);
    state.pressing=false;for(int n=0;n<24;++n)ht_station_step(&state);
    assert(!state.crease);ht_station_study_render(&frozen,&state);ht_pack_mono(bits,120);
    assert(!memcmp(base,bits,HT_NATIVE_PIXELS/8));
   }
  }
 }
 assert(ht_evidence_platform(3,0)==2 && ht_evidence_x(3,0)==HT_STATION_TICKET_X);
 reset();ht.x=662*256;assert(!ht_station_near(&ht));
 free(bits);free(base);free(copy);free(memory);
 puts("Station: real ticket location, HID/XInput hold/release fold, four comparisons, pause/journal/fault/resume, repeat/cancel/exit and immutable raster PASS");
}
