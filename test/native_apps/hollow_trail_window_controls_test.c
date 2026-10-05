/* Real HID/XInput interruption and resume of the signal-window attachment. */
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
 ht.x=2040*256;ht.y=ht_signal_window_floor()*256;ht.grounded=true;
 ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=8;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;
 ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=false;app=&fake_app;
 ht_input(1);input(0,2);ticks(100);input(0,8);ticks(8);
 assert(ht.x==(HT_SIGNAL_WINDOW_LEFT-5)*256 && !ht.traversal.window_open);
}
int main(void) {
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY);assert(memory);ht_bind(memory);ht_bind_native(memory);
 for(source=0;source<2;++source) {
  reset();action();assert(ht.traversal.mode==HT_WINDOW);
  input(0,2);ticks(70);input(0,8);assert(ht.traversal.window_open==32 && !ht.grounded);
  press(source?64:256);ticks(1);assert(paused);ht_game frozen=ht;
  ticks(90);assert(!memcmp(&ht,&frozen,sizeof(ht)));
  press(source?64:256);ticks(1);assert(!paused && ht.traversal.window_phase==frozen.traversal.window_phase);
  press(source?128:512);assert(reading);frozen=ht;ticks(90);assert(!memcmp(&ht,&frozen,sizeof(ht)));
  cancel();assert(!reading);input(0,8);
  action();assert(ht.traversal.mode==HT_WINDOW);press(source?1:2);ticks(1);assert(ht.traversal.mode==HT_WINDOW);
  input(0,6);for(unsigned n=0;ht.traversal.mode==HT_WINDOW && n<160;++n)ticks(1);
  input(0,8);assert(ht.traversal.mode==HT_FREE && ht.grounded && ht.traversal.window_open==32);
  assert(!ht.evidence && !ht.puzzle.solved);
  input(0,2);ticks(3);input(0,8);action();assert(ht.traversal.mode==HT_WINDOW);
  input(0,2);for(unsigned n=0;ht.traversal.mode==HT_WINDOW && n<160;++n)ticks(1);
  input(0,8);assert(ht.traversal.mode==HT_FREE && ht.x==(HT_SIGNAL_WINDOW_RIGHT+5)*256);
  input(0,2);for(unsigned n=0;ht.x<HT_SIGNAL_LOG_X*256 && n<180;++n)ticks(1);
  input(0,8);action();assert(ht_signal_room_studying && ht_evidence_found(&ht,5));
  reset();action();input(0,2);ticks(90);input(0,8);host_exit=true;ht_input(1);assert(quitting);
 }
 free(memory);puts("Window controls: HID/XInput closed stop, A attachment, pause/journal hold, no jump detachment, reverse/reenter, log handoff and exit PASS");
}
