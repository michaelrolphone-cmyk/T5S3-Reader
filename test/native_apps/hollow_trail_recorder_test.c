/* Actual storm-gallery route into the protected manual, then original distributor. */
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
 memset(&ht,0,sizeof(ht));ht.level=7;ht_select_level(7);ht_spawn(true);
 ht_recorder=(ht_recorder_state){0};ht_warming=(ht_warming_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;walk_phase=0;for(unsigned i=0;i<20000 && !ht_recorder_near(&ht);++i)walk_route_tick();
 assert(ht.level==7 && !ht.deaths && ht_recorder_near(&ht));
 assert(!ht_evidence_found(&ht,23) && !(ht.scene_evidence&4));
 ht_input(1);input(0,8);
}
static void reach(const ht_person_pose *p,unsigned tick){
 for(int i=0;i<2;++i){int dx=p->hand[i].x-p->shoulder.x,dy=p->hand[i].y-p->shoulder.y;
  if(dx*dx+dy*dy>144)fprintf(stderr,"arm tick=%u i=%d dx=%d dy=%d\n",tick,i,dx,dy);
  assert(dx*dx+dy*dy<=144);dx=p->foot[i].x-p->hip.x;dy=p->foot[i].y-1-p->hip.y;
  if(dx*dx+dy*dy>225)fprintf(stderr,"leg tick=%u i=%d dx=%d dy=%d\n",tick,i,dx,dy);
  assert(dx*dx+dy*dy<=225);
 }
}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*mono=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&mono);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 for(source=0;source<2;++source){
  reset();assert(!ht_evidence_found(&ht,23));uint32_t prior=ht.evidence;action();
  assert(ht_recorder.active && !reading && !ht_recorder.stage && ht.evidence==(prior|(1u<<23)) && (ht.scene_evidence&4) && !ht_weather_target(&ht));ht_game frozen=ht;
  assert(!ht.puzzle.solved && !ht.puzzle.opening);input(0,8);ticks(230);assert(!ht_recorder.stage);
  for(unsigned stage=0;stage<8;stage+=2){
   action();input(0,8);ticks(20);assert(ht_recorder.stage==stage+1);unsigned tick=ht_recorder.tick;
   /* Repeated confirmation and held directions cannot skip physical work. */
   action();input(0,2);ticks(4);input(0,8);assert(ht_recorder.stage==stage+1);
   press(source?64:256);ht_recorder_state stopped=ht_recorder;ticks(60);action();assert(!memcmp(&ht_recorder,&stopped,sizeof(stopped)));press(source?64:256);input(0,8);
   press(source?128:512);assert(reading);stopped=ht_recorder;ticks(60);assert(!memcmp(&ht_recorder,&stopped,sizeof(stopped)));
   assert(!ht_journal_index && journal_page==23);present();press(source?128:512);input(0,8);assert(!reading);
   pad_failure=true;input(0,8);stopped=ht_recorder;ticks(30);assert(!memcmp(&ht_recorder,&stopped,sizeof(stopped)));pad_failure=false;input(source?2:1,2);ticks(30);assert(ht_input_rearm && !memcmp(&ht_recorder,&stopped,sizeof(stopped)));input(0,8);
   for(unsigned n=0;n<220 && ht_recorder.stage==stage+1;++n)ticks(1);
   assert(ht_recorder.stage==stage+2 && !ht_recorder.tick && !memcmp(&ht,&frozen,sizeof(ht)));ticks(100);assert(ht_recorder.stage==stage+2);(void)tick;
  }
  action();assert(reading && !ht_journal_index && journal_page==23);
  /* Existing submitted-page guards: premature confirm cannot read the note. */
  unsigned page=ht_journal_leaf;input(0,2);assert(ht_journal_leaf==page && !ht.verdict_read && !ht.door_notebook);present();
  press(source?128:512);input(0,8);assert(!reading);cancel();assert(!ht_recorder.active && !memcmp(&ht,&frozen,sizeof(ht)));
  input(source?2:1,2);ticks(4);assert(ht_input_rearm && !ht_recorder.active);input(0,8);action();assert(ht_recorder.active && !ht_recorder.stage);cancel();
  for(unsigned stage=0;stage<=8;++stage){reset();action();ht_recorder.stage=stage;ht_recorder.tick=20;frozen=ht;input(0,8);cancel();assert(!ht_recorder.active && !memcmp(&ht,&frozen,sizeof(ht)));input(0,8);action();assert(ht_recorder.active && !ht_recorder.stage && !ht_recorder.tick);cancel();}
  reset();action();host_exit=true;input(0,8);assert(quitting);
 }
 source=0;reset();ht_game view=ht;view.camera=(HT_RECORDER_X-230)*256;view.camera_y=(ht_recorder_floor()-190)*256;
 for(unsigned stage=0;stage<=8;++stage)for(unsigned tick=0;tick<=ht_recorder_limit(stage);++tick){
  ht_recorder_state s={true,false,(uint8_t)stage,(uint16_t)tick};ht_person_pose p=ht_recorder_pose(&view,&s);reach(&p,tick);
  if(stage>=2 || (stage==1 && tick>=64)){ht_joint c=ht_recorder_latch();assert(p.hand[1].x+view.camera/256==c.x && p.hand[1].y+view.camera_y/256==c.y);}
 }
 assert(ht_recorder_trace(0).x==163 && ht_recorder_trace(0).y==107);
 assert(ht_recorder_trace(48).x==206 && ht_recorder_trace(48).y==135);
 assert(ht_recorder_trace(144).x==131 && ht_recorder_trace(144).y==140);
 for(int native=0;native<2;++native){reset();action();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned stage=0;stage<=8;++stage){ht_recorder_state s={true,false,(uint8_t)stage,48};ht_recorder_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(bits,120);ht_recorder_study_render(&ht,&s);assert(!memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && !memcmp(&ht,&frozen,sizeof(ht)));}
  ht_recorder_state s={true,false,4,0};ht_recorder_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(mono,120);s.stage=6;ht_recorder_study_render(&ht,&s);ht_pack_mono(bits,120);assert(memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && memcmp(bits,mono,HT_NATIVE_PIXELS/8));
 }
 /* Cancel to the same shelf and use the original cold-start/RUN route. */
 reset();action();input(0,8);cancel();input(0,8);
 assert((ht.scene_evidence&4) && ht_evidence_found(&ht,23));
 for(unsigned n=0;n<30000 && ht.x<(HT_GOAL-15)*256;++n)walk_route_tick();
 assert(ht.level==7 && !ht.deaths && ht.puzzle.solved && ht.puzzle.stage && ht.puzzle.opening==48);
 assert(ht_evidence_found(&ht,21) && ht_evidence_found(&ht,22) && ht_evidence_found(&ht,23));
 /* Mapped device navigation opens the same dry cabinet and does not quit
  * the app when Back cancels an inspection. */
 reset();pad=hid_pad=NULL;input(0,8);mapped=0;ht_input(1);mapped=T5_APP_BUTTON_CONFIRM;ht_input(1);mapped=0;ht_input(1);assert(ht_recorder.active && !ht_recorder.stage);
 mapped=T5_APP_BUTTON_CONFIRM;ht_input(1);mapped=0;ht_input(1);ticks(98);assert(ht_recorder.stage==2);
 ht_game frozen=ht;mapped=T5_APP_BUTTON_BACK;ht_input(1);assert(!ht_recorder.active && !quitting && !memcmp(&ht,&frozen,sizeof(ht)));
 free(mono);free(bits);free(copy);free(memory);puts("Recorder manual: actual storm route, dry cabinet/latch, printed shutter linkage, HID/XInput/mapped interruption/neutral/replay/read guards, original discovery/storm bit and cold-start/RUN route PASS");return 0;
}
