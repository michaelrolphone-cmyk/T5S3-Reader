/* Actual quarry route and original solved upper mechanism; no granted balance. */
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
 memset(&ht,0,sizeof(ht));ht.level=5;ht_select_level(5);ht_spawn(true);
 ht_cage=(ht_cage_state){0};ht_grip=(ht_grip_state){0};ht_hoist=(ht_hoist_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;for(unsigned i=0;i<20000 && !ht.puzzle.solved;++i)walk_route_tick();
 assert(ht.level==5 && !ht.deaths && ht.puzzle.solved && ht_puzzle_near(&ht)==3);
 /* Wait on this real platform for the mechanism's original opening. */
 for(unsigned i=0;i<48;++i)ht_step_controls(0,0,false,false);
 assert(ht_empty_cage_near(&ht));ht_input(1);input(0,8);
}
static void ready(void){action();input(0,8);ticks(66);assert(ht_cage.active && ht_cage.stage==HT_CAGE_CONTROL && !ht_cage.travel);}
static void reach(const ht_person_pose *p,unsigned tick){
 for(int i=0;i<2;++i){int dx=p->hand[i].x-p->shoulder.x,dy=p->hand[i].y-p->shoulder.y;
  if(dx*dx+dy*dy>144)fprintf(stderr,"arm tick=%u i=%d dx=%d dy=%d\n",tick,i,dx,dy);
  assert(dx*dx+dy*dy<=144);dx=p->foot[i].x-p->hip.x;dy=p->foot[i].y-1-p->hip.y;
  if(dx*dx+dy*dy>225)fprintf(stderr,"leg tick=%u i=%d dx=%d dy=%d\n",tick,i,dx,dy);
  assert(dx*dx+dy*dy<=225);
 }
}
static void leave_and_board(void){
 input(0,8);cancel();input(0,8);ticks(100);assert(!ht_cage.active && !ht_cage.travel && !ht_hoist.active);
 input(source?2:1,2);ticks(4);assert(ht_input_rearm && !ht_hoist.active && ht.level==5);
 input(0,8);
 for(unsigned n=0;n<1000 && ht.level==5;++n){input(0,2);ticks(1);}
 assert(ht.level==6 && ht_hoist.active);ht_select_level(6);assert(!memcmp(&ht,&ht_hoist.destination,sizeof(ht)));
}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*mono=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&mono);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 for(source=0;source<2;++source){
  reset();ht_game frozen=ht;assert(ht_evidence_found(&ht,15) && ht_evidence_found(&ht,16) && !ht_evidence_found(&ht,17));
  ht_game bad=ht;bad.puzzle.solved=false;assert(!ht_empty_cage_near(&bad));bad=ht;bad.puzzle.opening=47;assert(!ht_empty_cage_near(&bad));
  bad=ht;bad.puzzle.value[0]=3;assert(!ht_empty_cage_near(&bad));bad=ht;bad.puzzle.value[1]=0;assert(!ht_empty_cage_near(&bad));
  bad=ht;bad.grounded=false;assert(!ht_empty_cage_near(&bad));bad=ht;bad.x=ht_grip_x()*256;assert(!ht_empty_cage_near(&bad));
  bad=ht;bad.traversal.mode=HT_LADDER;assert(!ht_empty_cage_near(&bad));bad=ht;bad.level=6;assert(!ht_empty_cage_near(&bad));
  ready();assert(!memcmp(&ht,&frozen,sizeof(ht)) && !ht_hoist.active);
  ticks(1000);assert(!ht_cage.travel && !ht_cage.cycles);
  for(unsigned cycle=0;cycle<3;++cycle){
   input(0,2);ticks(39);input(0,8);assert(ht_cage.travel>=38 && ht_cage.travel<=40);unsigned travel=ht_cage.travel;
   ticks(500);assert(ht_cage.travel==travel); /* Releasing the handle stops at an arbitrary height. */
   input(0,2);ticks(9);action();input(0,8);travel=ht_cage.travel;ticks(300);assert(ht_cage.travel==travel);
   input(0,2);ticks(400);assert(ht_cage.travel==HT_CAGE_STROKE);unsigned rev=scene_revision;ticks(300);assert(scene_revision==rev);
   input(0,8);input(0,6);ticks(29);input(0,8);assert(ht_cage.travel<HT_CAGE_STROKE && ht_cage.travel>0);travel=ht_cage.travel;
   press(source?64:256);assert(ht_cage.paused);ht_cage_state stopped=ht_cage;ticks(60);action();assert(!memcmp(&ht_cage,&stopped,sizeof(stopped)));press(source?64:256);input(0,8);
   press(source?128:512);assert(reading && ht_journal_index);stopped=ht_cage;ticks(60);assert(!memcmp(&ht_cage,&stopped,sizeof(stopped)));present();press(source?128:512);input(0,8);assert(!reading && ht_cage.travel==travel);
   input(0,6);ticks(3);pad_failure=true;input(0,8);stopped=ht_cage;ticks(30);assert(!memcmp(&ht_cage,&stopped,sizeof(stopped)));
   pad_failure=false;input(source?2:1,2);ticks(30);assert(ht_input_rearm && !memcmp(&ht_cage,&stopped,sizeof(stopped)));input(0,8);assert(!ht_cage.direction);
   ticks(60);assert(ht_cage.travel==stopped.travel);input(0,6);ticks(150);input(0,8);assert(!ht_cage.travel && ht_cage.cycles==ht_min(cycle+1,2));
   assert(!memcmp(&ht,&frozen,sizeof(ht)) && !ht_hoist.active);
  }
  action();input(0,8);ticks(300);assert(ht_cage.stage==HT_CAGE_CABLE && ht_cage.tick==96);unsigned rev=scene_revision;ticks(300);assert(scene_revision==rev && !memcmp(&ht,&frozen,sizeof(ht)));
  input(0,2);ticks(20);assert(ht_cage.stage==HT_CAGE_CONTROL && ht_cage.travel);leave_and_board();
  /* Both cancellation before touching the handle and bypassing the entire
   * trial preserve the same original boarding route. */
  reset();frozen=ht;action();input(0,8);cancel();assert(!ht_cage.active && !memcmp(&ht,&frozen,sizeof(ht)));input(0,8);ready();cancel();assert(!ht_cage.active);
  reset();for(unsigned n=0;n<1000 && ht.level==5;++n){input(0,2);ticks(1);}assert(ht.level==6 && ht_hoist.active && !ht_cage.active);
  reset();ready();input(0,2);ticks(55);cancel();input(0,8);assert(ht_cage.stage==HT_CAGE_RETURN);frozen=ht;
  press(source?64:256);/* Recovery obeys pause and never boards. */
  ht_cage_state parked=ht_cage;ticks(200);assert(!memcmp(&ht_cage,&parked,sizeof(parked)));press(source?64:256);input(0,8);for(unsigned n=0;n<100 && ht_cage.active;++n)ticks(1);assert(!ht_cage.active && !ht_cage.travel && !memcmp(&ht,&frozen,sizeof(ht)));
  input(0,8);ready();host_exit=true;input(0,8);assert(quitting);
 }
 /* Every world-space hand and foot remains within the existing person rig;
  * the same helper supplies the visible handle and cage/cable contacts. */
 source=0;reset();ht_game view=ht;view.camera=(HT_PUZZLE_FIRST-58)*256;view.camera_y=(ht_level_land(5)[9].top-180)*256;
 for(unsigned tick=0;tick<=64;++tick){ht_cage_state s={.active=true,.stage=HT_CAGE_REACH,.tick=(uint16_t)tick};ht_person_pose p=ht_cage_pose(&view,&s);reach(&p,tick);}
 for(unsigned travel=0;travel<=HT_CAGE_STROKE;++travel){
  ht_cage_state s={.active=true,.stage=HT_CAGE_CONTROL,.travel=(uint8_t)travel};view.cage_view_travel=travel;
  ht_person_pose p=ht_cage_pose(&view,&s);reach(&p,travel);ht_joint grip=ht_empty_cage_grip(&view);
  assert(p.hand[1].x==grip.x-view.camera/256 && p.hand[1].y==grip.y-view.camera_y/256);
  for(int i=0;i<2;++i)assert(p.foot[i].y==ht_surface_at(&view,9,p.foot[i].x+view.camera/256)-view.camera_y/256);
  ht_joint roof=ht_empty_cage_roof(travel/3),pulley=ht_empty_cage_pulley();assert(roof.x==pulley.x && roof.y>pulley.y);
 }
 for(int native=0;native<2;++native){reset();ready();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned stage=0;stage<4;++stage){ht_cage_state s={.active=true,.stage=(uint8_t)stage,.travel=48,.tick=48};ht_cage_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(bits,120);ht_cage_study_render(&ht,&s);assert(!memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && !memcmp(&ht,&frozen,sizeof(ht)));}
  ht_cage_state s={.active=true,.stage=HT_CAGE_CONTROL};ht_cage_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(mono,120);s.travel=HT_CAGE_STROKE;ht_cage_study_render(&ht,&s);ht_pack_mono(bits,120);assert(memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && memcmp(bits,mono,HT_NATIVE_PIXELS/8));
 }
 /* Mapped device controls exercise the same open/lower/release/raise path. */
 reset();pad=hid_pad=NULL;input(0,8);mapped=0;ht_input(1);mapped=T5_APP_BUTTON_CONFIRM;ht_input(1);mapped=0;ht_input(1);ticks(66);assert(ht_cage.active);
 mapped=T5_APP_BUTTON_RIGHT;ht_input(1);ticks(20);mapped=0;ht_input(1);unsigned travel=ht_cage.travel;ticks(40);assert(ht_cage.travel==travel && travel);
 mapped=T5_APP_BUTTON_LEFT;ht_input(1);ticks(50);mapped=0;ht_input(1);assert(!ht_cage.travel);mapped=T5_APP_BUTTON_BACK;ht_input(1);assert(!ht_cage.active && !quitting);
 free(mono);free(bits);free(copy);free(memory);puts("Empty cage: real six-load solution, optional route, bounded lower/stop/raise/repeat/cable/return, shared contacts, immutable state and HID/XInput/mapped interruption/recovery PASS");return 0;
}
