/* Ridge scarf reached on the original support-one service pavement. */
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
 memset(&ht,0,sizeof(ht));ht.level=8;ht_select_level(8);ht_spawn(true);
 ht_scarf=(ht_scarf_state){0};ht_tower=(ht_tower_state){0};ht_counts=(ht_counts_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source==0?&gamepad:NULL;hid_pad=source==1?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;for(unsigned i=0;i<20000 && !ht_scarf_near(&ht);++i)walk_route_tick();
 assert(ht.level==8 && !ht.deaths && ht_scarf_near(&ht));
 ht_input(1);input(0,8);
}
static void pause_input(void){if(source<2)press(source?64:256);else {mapped=T5_APP_BUTTON_DOWN;input(0,8);mapped=0;input(0,8);}}
static void journal(void){if(source<2)press(source?128:512);else {mapped=T5_APP_BUTTON_CONFIRM;input(0,8);mapped=0;input(0,8);}}
static void reach_stage(unsigned stage){
 while(ht_scarf.stage<stage){if(!(ht_scarf.stage&1))action();input(0,8);ticks(1);}
 assert(ht_scarf.stage==stage);
}
static void unchanged_except_scarf(ht_game *frozen){frozen->scarf=ht.scarf;assert(!memcmp(&ht,frozen,sizeof(ht)));}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=calloc(1,HT_NATIVE_PIXELS/8),*mono=calloc(1,HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&mono);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 assert(ht_evidence_x(8,0)==552 && ht_evidence_platform(8,0)==1);
 for(source=0;source<3;++source){
  reset();assert(!ht_evidence_found(&ht,24) && !ht_weather_target(&ht));uint32_t prior=ht.evidence;action();
  assert(ht_scarf.active && !reading && !ht_scarf.stage && ht.evidence==(prior|(1u<<24)) && (ht.scene_evidence&1));ht_game frozen=ht;assert(ht_weather_target(&ht)==256);
  assert(!ht.puzzle.solved && !ht.puzzle.opening);input(0,8);ticks(160);assert(!ht_scarf.stage && !ht.scarf);
  for(unsigned stage=0;stage<10;stage+=2){
   action();input(0,8);ticks(12);assert(ht_scarf.stage==stage+1);
   action();input(0,2);ticks(4);input(0,8);assert(ht_scarf.stage==stage+1);
   pause_input();ht_scarf_state stopped=ht_scarf;ticks(60);action();assert(!memcmp(&ht_scarf,&stopped,sizeof(stopped)));pause_input();input(0,8);
   if(source<2){
    journal();assert(reading && !ht_journal_index && journal_page==24);stopped=ht_scarf;ticks(60);assert(!memcmp(&ht_scarf,&stopped,sizeof(stopped)));
    unsigned leaf=ht_journal_leaf;input(0,2);assert(ht_journal_leaf==leaf && !ht.verdict_read && !ht.door_notebook);present();journal();input(0,8);assert(!reading);
    pad_failure=true;input(0,8);stopped=ht_scarf;ticks(30);assert(!memcmp(&ht_scarf,&stopped,sizeof(stopped)));pad_failure=false;input(source?2:1,2);ticks(30);assert(ht_input_rearm && !memcmp(&ht_scarf,&stopped,sizeof(stopped)));input(0,8);
   }
   for(unsigned n=0;n<220 && ht_scarf.stage==stage+1;++n)ticks(1);
   assert(ht_scarf.stage==stage+2 && !ht_scarf.tick);unchanged_except_scarf(&frozen);
   assert(ht.scarf==(stage<2?0:stage<8?1:2));ticks(100);assert(ht_scarf.stage==stage+2);
  }
  action();assert(reading && journal_page==24);present();cancel();input(0,8);assert(reading && ht_journal_index);cancel();input(0,8);assert(!reading);cancel();assert(!ht_scarf.active);unchanged_except_scarf(&frozen);
  for(unsigned stage=0;stage<=10;++stage){
   reset();action();reach_stage(stage);input(0,8);ticks(20);frozen=ht;cancel();assert(!ht_scarf.active && !memcmp(&ht,&frozen,sizeof(ht)));
   unsigned expected=ht.scarf==2?10:ht.scarf?4:0;input(0,8);action();assert(ht_scarf.active && ht_scarf.stage==expected && !ht_scarf.tick);cancel();
  }
  reset();action();host_exit=true;input(0,8);assert(quitting);
 }
 source=0;reset();ht_game view=ht;view.camera=(ht_scarf_x()-225)*256;view.camera_y=(ht_scarf_floor()-192)*256;
 for(unsigned tick=0;tick<=64;++tick){ht_scarf_state s={true,false,1,(uint16_t)tick};ht_person_pose p=ht_scarf_pose(&view,&s);
  for(int i=0;i<2;++i){int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;assert(dx*dx+dy*dy<=144);
   assert(p.foot[i].y==ht_surface_at(&view,1,p.foot[i].x+view.camera/256)-view.camera_y/256);}
  if(tick==64){ht_joint k=ht_scarf_knot();assert(p.hand[1].x==k.x-view.camera/256 && p.hand[1].y==k.y-view.camera_y/256);}
 }
 {ht_game g=ht;assert(ht_scarf_near(&g));g.grounded=false;assert(!ht_scarf_near(&g));g=ht;g.traversal.mode=HT_LADDER;assert(!ht_scarf_near(&g));g=ht;g.x-=50*256;assert(!ht_scarf_near(&g));}
 /* Every transition shares its endpoints with the adjoining hold. The two
  * backward turns remain physically side by side on cloth and drawing. */
 for(unsigned stage=3;stage<=9;stage+=2){
  ht_scarf_state a={true,false,(uint8_t)stage,96},b={true,false,(uint8_t)(stage+1),0};
  for(int side=0;side<2;++side){ht_joint x=ht_scarf_end(&a,side),y=ht_scarf_end(&b,side);assert(x.x==y.x && x.y==y.y);}
 }
 {ht_scarf_state s={true,false,6,0};ht_joint a=ht_scarf_thread(&s),b=ht_scarf_ink();assert(ht_abs(a.x-b.x)<=2 && a.y-b.y==26);}
 for(int native=0;native<2;++native){reset();action();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned stage=0;stage<=10;++stage){ht_scarf_state s={true,false,(uint8_t)stage,48};ht_scarf_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(bits,120);ht_scarf_study_render(&ht,&s);assert(!memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && !memcmp(&ht,&frozen,sizeof(ht)));}
  const unsigned pairs[][2]={{2,4},{4,6},{6,8},{8,9}};
  for(unsigned n=0;n<4;++n){ht_scarf_state s={true,false,(uint8_t)pairs[n][0],0};ht_scarf_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(mono,120);s.stage=pairs[n][1];s.tick=48;ht_scarf_study_render(&ht,&s);ht_pack_mono(bits,120);assert(memcmp(copy,ht_scene,native?HT_NATIVE_SCALE*HT_NATIVE_W*220:HT_W*220) && memcmp(bits,mono,HT_NATIVE_SCALE*HT_NATIVE_W*220/8));}
  /* Taking removes the post's sole cloth; wearing removes the hand's strip. */
  view=ht;view.camera=(ht_scarf_x()-225)*256;view.camera_y=(ht_scarf_floor()-192)*256;
  size_t bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
  ht_frame_camera(&view,HT_CAMERA_BASELINE);ht_render_scene_from(&view,false);memcpy(copy,ht_scene,bytes);view.scarf=1;ht_render_scene_from(&view,false);assert(memcmp(copy,ht_scene,bytes));
  memcpy(copy,ht_scene,bytes);view.scarf=2;ht_render_scene_from(&view,false);assert(!memcmp(copy,ht_scene,bytes));
  view.scarf=1;ht_render_scene_from(&view,true);memcpy(copy,ht_scene,bytes);view.scarf=2;ht_render_scene_from(&view,true);assert(memcmp(copy,ht_scene,bytes));
 }
 /* The actual declined path reaches the tower with the strip still tied. */
 reset();action();input(0,8);cancel();input(0,8);assert(!ht.scarf);
 for(unsigned n=0;n<60000 && !ht_tower_near(&ht);++n)walk_route_tick();
 assert(ht.level==9 && ht_tower_near(&ht) && !ht.deaths && !ht.scarf);
 ht_render_scene_from(&ht,true); /* Normal chapter render admits its input geometry. */
 action();assert(ht_tower.active && !ht_tower.card);
 for(unsigned stage=0;stage<6;stage+=2){action();input(0,8);for(unsigned n=0;n<130 && ht_tower.stage==stage+1;++n)ticks(1);assert(ht_tower.stage==stage+2);}
 {ht_game frozen=ht;action();input(0,8);ticks(120);assert(ht_tower.stage==6 && !memcmp(&ht,&frozen,sizeof(ht)));
  for(int native=0;native<2;++native){ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_tower_study_render(&ht,&ht_tower);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);
   ht_tower_state invalid=ht_tower;invalid.stage=8;ht_tower_study_render(&ht,&invalid);assert(!memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS));}
  assert(!ht.scarf);cancel();input(0,8);assert(!ht_tower.active && !ht.scarf);
 }
 /* Weather still starts at discovery, then ends at page 25. The complete
  * original route and isolated-return chime sequence are unchanged. */
 reset();action();reach_stage(10);input(0,8);cancel();input(0,8);
 for(unsigned n=0;n<30000 && ht.x<(HT_GOAL-15)*256;++n)walk_route_tick();
 assert(ht.level==8 && !ht.deaths && ht.puzzle.solved && ht.puzzle.opening==48 && ht_evidence_found(&ht,24) && ht_evidence_found(&ht,25) && ht_evidence_found(&ht,26));
 assert(!ht_weather_target(&ht) && ht.puzzle.stage==1 && ht.scarf==2 && !ht.verdict && !ht.verdict_read);
 ht_spawn(false);assert(ht.scarf==2);ht.level=9;ht_select_level(9);ht_spawn(true);assert(ht.scarf==2);
 /* The pre-existing tower scene consumes the same worn strip. Reopening
  * after folding does not conjure another piece around the neck. */
 ht_tower_begin(false);ht_tower.stage=6;assert(ht_tower_action(&ht_tower) && ht_tower.stage==7);
 for(unsigned n=0;n<96;++n){assert(ht_tower_step(&ht_tower));}assert(ht.scarf==1 && ht_tower.stage==8);
 assert(ht_tower_action(&ht_tower));for(unsigned n=0;n<96;++n){assert(ht_tower_step(&ht_tower));}assert(ht.scarf==3 && ht_tower.stage==10);
 ht_tower_begin(false);ht_tower.stage=6;assert(ht_tower_action(&ht_tower) && ht_tower.stage==10);
 ht_spawn(false);assert(ht.scarf==3);ht.level=0;ht_select_level(0);ht_spawn(true);assert(!ht.scarf);
 free(mono);free(bits);free(copy);free(memory);puts("Ridge scarf: original reachable page24, deliberate feel/untie/compare/wrap, grounded contacts, one strip across cancel/reentry/death/tower, HID/XInput/mapped pause/journal/fault/read guards, immutable gray/mono rendering and unchanged isolation/chime route PASS");return 0;
}
