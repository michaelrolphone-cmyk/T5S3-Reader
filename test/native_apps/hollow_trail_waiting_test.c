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
 ht.x=HT_WAITING_X*256;ht.y=ht_waiting_floor()*256;ht.grounded=true;
 ht_waiting=(ht_waiting_state){0};ht_distribution=(ht_distribution_state){0};ht_station=(ht_station_state){0};ht_stove=(ht_stove_state){0};
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
  reset();action();assert(ht_waiting.active && ht_evidence_found(&ht,13));ht_game frozen=ht;
  input(0,8);unsigned rev=scene_revision;ticks(40);assert(scene_revision==rev && !ht_waiting.stage);
  action();ticks(120);assert(ht_waiting.stage==1 && ht_waiting.tick>=104);
  action();assert(ht_waiting.tick>=104);input(0,8);
  press(source?64:256);assert(ht_waiting.paused);unsigned at=ht_waiting.tick;
  ticks(60);assert(ht_waiting.tick==at);press(source?64:256);
  press(source?128:512);assert(reading && ht_waiting.active);ticks(60);assert(ht_waiting.tick==at);
  input(0,8);press(source?128:512);input(0,8);assert(!reading);
  pad_failure=true;input(0,8);at=ht_waiting.tick;ticks(60);assert(ht_waiting.tick==at);
  pad_failure=false;input(0,8);input(0,8);ticks(650);
  assert(ht_waiting.stage==2 && ht_waiting.tick==600);rev=scene_revision;ticks(500);assert(scene_revision==rev);
  assert(!memcmp(&ht,&frozen,sizeof(ht)));
  action();assert(reading);input(0,8);press(source?128:512);input(0,8);
  cancel();assert(!ht_waiting.active);input(0,8);action();assert(!ht_waiting.stage);
  host_exit=true;input(0,8);assert(quitting);
 }
 for(int native=0;native<2;++native){
  reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  ht_waiting_state state={true,false,0,0};ht_waiting_study_render(&frozen,&state);
  int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;memcpy(copy,ht_scene,bytes);
  ht_waiting_study_render(&frozen,&state);assert(!memcmp(copy,ht_scene,bytes));
  state.stage=1;state.tick=116;ht_waiting_study_render(&frozen,&state);assert(memcmp(copy,ht_scene,bytes));
  state.stage=2;state.tick=600;ht_waiting_study_render(&frozen,&state);memcpy(copy,ht_scene,bytes);
  for(int n=0;n<10000;++n)assert(!ht_waiting_step(&state));
  ht_waiting_study_render(&frozen,&state);assert(!memcmp(copy,ht_scene,bytes)&&!memcmp(&ht,&frozen,sizeof(ht)));
  for(unsigned tick=0;tick<=600;++tick){
   state.stage=tick?1:0;state.tick=(uint16_t)tick;
   ht_person_pose p=ht_waiting_pose(&frozen,&state);
   for(int i=0;i<2;++i){int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;assert(dx*dx+dy*dy<=144);
    dx=p.foot[i].x-p.hip.x;dy=p.foot[i].y-1-p.hip.y;if(dx*dx+dy*dy>225)fprintf(stderr,"tick=%u foot=%d dx=%d dy=%d hip=%d,%d foot=%d,%d\n",tick,i,dx,dy,p.hip.x,p.hip.y,p.foot[i].x,p.foot[i].y);assert(dx*dx+dy*dy<=225);}
  }
  ht_native_active=native!=0;ht_native_foreground_half_y=false;ht_world_scale=256;ht_scene=native?ht_native_a:ht_scene_low;
  ht_game v=frozen;v.camera=(HT_WAITING_X-200)*256;v.camera_y=(ht_waiting_floor()-180)*256;
  memset(ht_scene,110,bytes);ht_waiting_room(&v,0,0);ht_pack_mono(base,120);
  for(int n=0;n<bytes;++n)if(ht_scene[n]==99)ht_scene[n]=175;
  ht_pack_mono(bits,120);assert(memcmp(base,bits,HT_NATIVE_PIXELS/8));
 }
 assert(ht_waiting_swing(12)>0 && ht_waiting_swing(36)<0 && !ht_waiting_swing(240));

 free(base);free(bits);free(copy);free(memory);
 puts("Waiting awning: physical groove, one deliberate bell pull, damped silence, reachable limbs, HID/XInput interruption/retry/exit and static unanswered hold PASS");
}
