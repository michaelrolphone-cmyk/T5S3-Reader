/* Turbine gauge reached on the original support-one service pavement. */
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
static void action(void){if(source<2)press(source?2:1);else {mapped=0;input(0,8);mapped=T5_APP_BUTTON_CONFIRM;input(0,8);mapped=0;input(0,8);}}
static void cancel(void){if(source<2)press(source?4:8);else {mapped=0;input(0,8);mapped=T5_APP_BUTTON_BACK;input(0,8);mapped=0;input(0,8);}}
static void ticks(unsigned n){for(unsigned i=0;i<n;++i)ht_advance(now+=HT_STEP_MS);}
static void present(void){ht_journal_render();assert(ht_journal_page_ready);ht_read_submitted_revision=scene_revision;}
static void reset(void){
 memset(&ht,0,sizeof(ht));ht.level=7;ht_select_level(7);ht_spawn(true);
 ht_gauge=(ht_gauge_state){0};ht_counts=(ht_counts_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source==0?&gamepad:NULL;hid_pad=source==1?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;for(unsigned i=0;i<20000 && !ht_gauge_near(&ht);++i)walk_route_tick();
 assert(ht.level==7 && !ht.deaths && ht_gauge_near(&ht));
 ht_input(1);input(0,8);
}
static void pause_input(void){if(source<2)press(source?64:256);else {mapped=T5_APP_BUTTON_DOWN;input(0,8);mapped=0;input(0,8);}}
static void journal(void){if(source<2)press(source?128:512);else {mapped=T5_APP_BUTTON_CONFIRM;input(0,8);mapped=0;input(0,8);}}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*mono=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&mono);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 assert(ht_evidence_x(7,0)==542 && ht_evidence_platform(7,0)==1);
 for(source=0;source<3;++source){
  reset();assert(!ht_evidence_found(&ht,21) && !ht_weather_target(&ht));uint32_t prior=ht.evidence;action();
  assert(ht_gauge.active && !reading && !ht_gauge.stage && ht.evidence==(prior|(1u<<21)) && (ht.scene_evidence&1));ht_game frozen=ht;assert(!ht_weather_target(&ht));
  assert(!ht.puzzle.solved && !ht.puzzle.opening);input(0,8);ticks(160);assert(!ht_gauge.stage);
  for(unsigned stage=0;stage<8;stage+=2){
   action();input(0,8);ticks(12);assert(ht_gauge.stage==stage+1);
   action();input(0,2);ticks(4);input(0,8);assert(ht_gauge.stage==stage+1);
   pause_input();ht_gauge_state stopped=ht_gauge;ticks(60);action();assert(!memcmp(&ht_gauge,&stopped,sizeof(stopped)));pause_input();input(0,8);
   if(source<2){
    journal();assert(reading && !ht_journal_index && journal_page==21);stopped=ht_gauge;ticks(60);assert(!memcmp(&ht_gauge,&stopped,sizeof(stopped)));
    /* Unsubmitted text cannot acquire read/ending credit or change leaves. */
    unsigned leaf=ht_journal_leaf;input(0,2);assert(ht_journal_leaf==leaf && !ht.verdict_read && !ht.door_notebook);present();journal();input(0,8);assert(!reading);
    pad_failure=true;input(0,8);stopped=ht_gauge;ticks(30);assert(!memcmp(&ht_gauge,&stopped,sizeof(stopped)));pad_failure=false;input(source?2:1,2);ticks(30);assert(ht_input_rearm && !memcmp(&ht_gauge,&stopped,sizeof(stopped)));input(0,8);
   }
   for(unsigned n=0;n<220 && ht_gauge.stage==stage+1;++n)ticks(1);
   assert(ht_gauge.stage==stage+2 && !ht_gauge.tick && !memcmp(&ht,&frozen,sizeof(ht)));ticks(100);assert(ht_gauge.stage==stage+2);
  }
  action();assert(reading && journal_page==21);present();cancel();input(0,8);assert(reading && ht_journal_index);cancel();input(0,8);assert(!reading);cancel();assert(!ht_gauge.active && !memcmp(&ht,&frozen,sizeof(ht)));
  for(unsigned stage=0;stage<=8;++stage){reset();action();ht_gauge.stage=stage;ht_gauge.tick=20;frozen=ht;input(0,8);cancel();assert(!ht_gauge.active && !memcmp(&ht,&frozen,sizeof(ht)));input(0,8);action();assert(ht_gauge.active && !ht_gauge.stage && !ht_gauge.tick);cancel();}
  reset();action();host_exit=true;input(0,8);assert(quitting);
 }
 source=0;reset();ht_game view=ht;view.camera=(ht_gauge_x()-162)*256;view.camera_y=(ht_gauge_floor()-192)*256;
 for(unsigned tick=0;tick<=64;++tick){ht_gauge_state s={true,false,1,(uint16_t)tick};ht_person_pose p=ht_gauge_pose(&view,&s);
  for(int i=0;i<2;++i){int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;assert(dx*dx+dy*dy<=144);}
  if(tick==64){ht_joint c=ht_gauge_contact();assert(p.hand[1].x==c.x-view.camera/256 && p.hand[1].y==c.y-view.camera_y/256);}
 }
 /* Needle holds its reading in the scale plane while mica parallax separates
  * the accidental duplicate. Every intermediate fingertip is on one wire. */
 for(unsigned tick=0;tick<=96;++tick){ht_gauge_state s={true,false,3,(uint16_t)tick};unsigned h=ht_gauge_head(&s);ht_joint n=ht_gauge_needle(h),scratch=ht_gauge_scratch(h);
  assert(n.x-(248-(int)h/32)==-37 && n.y==80 && scratch.y==n.y);
  assert(scratch.x-n.x==3+(int)h/16);
 }
 for(unsigned tick=0;tick<=144;++tick){ht_joint p=ht_gauge_trace(tick);bool on=false;
  for(unsigned n=0;n<7;++n){ht_joint a=ht_gauge_feed_path[n],b=ht_gauge_feed_path[n+1];
   if(a.x==b.x)on|=p.x==a.x && p.y>=ht_min(a.y,b.y) && p.y<=ht_max(a.y,b.y);
   else on|=p.y==a.y && p.x>=ht_min(a.x,b.x) && p.x<=ht_max(a.x,b.x);
  }assert(on);
 }
 assert(ht_gauge_trace(60).x==365 && ht_gauge_trace(60).y==69);
 assert(ht_gauge_trace(140).x==365 && ht_gauge_trace(140).y==169);
 /* The examination is offered only at the original grounded find. */
 {ht_game g=ht;assert(ht_gauge_near(&g));g.grounded=false;assert(!ht_gauge_near(&g));g=ht;g.traversal.mode=HT_LADDER;assert(!ht_gauge_near(&g));g=ht;g.x-=50*256;assert(!ht_gauge_near(&g));}
 for(int native=0;native<2;++native){reset();action();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned stage=0;stage<=8;++stage){ht_gauge_state s={true,false,(uint8_t)stage,48};ht_gauge_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(bits,120);ht_gauge_study_render(&ht,&s);assert(!memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && !memcmp(&ht,&frozen,sizeof(ht)));}
  const unsigned pairs[][2]={{2,4},{4,6},{6,8},{7,8}};
  for(unsigned n=0;n<4;++n){ht_gauge_state s={true,false,(uint8_t)pairs[n][0],0};ht_gauge_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(mono,120);s.stage=pairs[n][1];ht_gauge_study_render(&ht,&s);ht_pack_mono(bits,120);assert(memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && memcmp(bits,mono,HT_NATIVE_PIXELS/8));}
  /* Reading around the casing uncovers its final words; head/casing movement
   * and the steady switch never alter live position or any circuit value. */
  ht_gauge_state casing={true,false,5,0};ht_gauge_study_render(&ht,&casing);ht_pack_mono(mono,120);casing.tick=96;ht_gauge_study_render(&ht,&casing);ht_pack_mono(bits,120);assert(memcmp(bits,mono,HT_NATIVE_PIXELS/8) && !memcmp(&ht,&frozen,sizeof(ht)));
 }
 reset();action();input(0,8);cancel();input(0,8);
 for(unsigned n=0;n<30000 && ht.x<(HT_GOAL-15)*256;++n)walk_route_tick();
 assert(ht.level==7 && !ht.deaths && ht.puzzle.solved && ht.puzzle.opening==48 && ht_evidence_found(&ht,21) && ht_evidence_found(&ht,22) && ht_evidence_found(&ht,23));
 assert(!ht_weather_target(&ht));
 assert(ht.puzzle.stage && ht.puzzle.value[0]==2 && ht.puzzle.value[1]==1 && ht.puzzle.value[2]==3);
 free(mono);free(bits);free(copy);free(memory);puts("Turbine gauge: original reachable page21, deliberate mica/casing/feed, grounded contacts, head-dependent parallax, finger follows both branches, HID/XInput/mapped pause/cancel/retry, journal/fault/read guards, frozen live state, gray/mono rendering and unchanged cold-start/RUN route PASS");return 0;
}
