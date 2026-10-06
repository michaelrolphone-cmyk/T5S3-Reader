/* Wind-shelter papers stay at their original support-four evidence site. */
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
 ht_scarf=(ht_scarf_state){0};ht_papers=(ht_papers_state){0};ht_tower=(ht_tower_state){0};ht_counts=(ht_counts_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source==0?&gamepad:NULL;hid_pad=source==1?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;for(unsigned i=0;i<20000 && !ht_papers_near(&ht);++i)walk_route_tick();
 assert(ht.level==8 && !ht.deaths && ht_papers_near(&ht));
 ht_input(1);input(0,8);
}
static void pause_input(void){if(source<2)press(source?64:256);else {mapped=T5_APP_BUTTON_DOWN;input(0,8);mapped=0;input(0,8);}}
static void journal(void){if(source<2)press(source?128:512);else {mapped=T5_APP_BUTTON_CONFIRM;input(0,8);mapped=0;input(0,8);}}
static void reach_stage(unsigned stage){
 while(ht_papers.stage<stage){if(!(ht_papers.stage&1))action();input(0,8);ticks(1);}
 assert(ht_papers.stage==stage);
}
static void retained(ht_game before){before.packet=ht.packet;assert(!memcmp(&ht,&before,sizeof(ht)));}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=calloc(1,HT_NATIVE_PIXELS/8),*mono=calloc(1,HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&mono);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 assert(ht_evidence_x(8,1)==1645 && ht_evidence_platform(8,1)==4 && ht_papers_floor()==140);
 for(source=0;source<3;++source)for(unsigned packet=0;packet<4;++packet){
  reset();ht.packet=(uint8_t)packet;ht.scarf=2;uint32_t evidence=ht.evidence;action();
  assert(ht_papers.active && !reading && !ht_papers.stage && ht.evidence==(evidence|(1u<<25)) && (ht.scene_evidence&2));
  assert(!ht_weather_target(&ht));ht_game frozen=ht;
  ticks(160);assert(!ht_papers.stage && ht.packet==packet);
  while(ht_papers.stage<14){unsigned from=ht_papers.stage;action();input(0,8);
   unsigned next=from==4 && !(packet&HT_PACKET_DOG)?6:from==10 && !(packet&HT_PACKET_FOOD)?12:from+1;
   assert(ht_papers.stage==next);
   if(next&1){ticks(10);action();input(0,2);ticks(4);input(0,8);assert(ht_papers.stage==next);
    pause_input();ht_papers_state stopped=ht_papers;ticks(40);action();assert(!memcmp(&stopped,&ht_papers,sizeof(stopped)));pause_input();input(0,8);
    if(source<2){journal();assert(reading && journal_page==25);stopped=ht_papers;ticks(40);assert(!memcmp(&stopped,&ht_papers,sizeof(stopped)));
     unsigned leaf=ht_journal_leaf;input(0,2);assert(ht_journal_leaf==leaf && !ht.verdict_read && !ht.door_notebook);present();journal();input(0,8);assert(!reading);
     pad_failure=true;input(0,8);stopped=ht_papers;ticks(40);assert(!memcmp(&stopped,&ht_papers,sizeof(stopped)));pad_failure=false;input(source?2:1,2);ticks(40);assert(ht_input_rearm && !memcmp(&stopped,&ht_papers,sizeof(stopped)));input(0,8);
    }
    for(unsigned i=0;i<220 && ht_papers.stage==next;++i){ticks(1);}assert(ht_papers.stage==next+1 && !ht_papers.tick);
   }
   retained(frozen);unsigned stage=ht_papers.stage;ticks(180);assert(ht_papers.stage==stage);
  }
  assert(ht.packet==(packet|((packet&HT_PACKET_FOOD)?HT_PACKET_CRUMBS:0)) && ht.scarf==2);
  action();assert(reading && journal_page==25);present();cancel();input(0,8);assert(reading && ht_journal_index);cancel();input(0,8);assert(!reading);cancel();assert(!ht_papers.active);retained(frozen);
  input(0,8);action();assert(ht_papers.active && !ht_papers.food);cancel();input(0,8);
 }
 for(source=0;source<3;++source)for(unsigned stage=0;stage<=14;++stage){
  reset();ht.packet=HT_PACKET_DOG|HT_PACKET_FOOD;action();reach_stage(stage);input(0,8);ticks(12);ht_game frozen=ht;cancel();assert(!ht_papers.active && !memcmp(&frozen,&ht,sizeof(ht)));
  input(0,8);action();assert(ht_papers.active && !ht_papers.stage && ht_papers.dog && ht_papers.food==!(ht.packet&HT_PACKET_CRUMBS));cancel();
 }
 source=0;reset();ht_game view=ht;view.camera=(ht_papers_x()-270)*256;view.camera_y=(ht_papers_floor()-191)*256;
 for(unsigned stage=0;stage<=14;++stage)for(unsigned tick=0;tick<=ht_papers_limit(stage);++tick){ht_papers_state s={true,false,true,true,(uint8_t)stage,(uint16_t)tick};ht_person_pose p=ht_papers_pose(&view,&s);
  for(int i=0;i<2;++i){int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;assert(dx*dx+dy*dy<=289);int wx=p.foot[i].x+view.camera/256;assert(p.foot[i].y==ht_surface_at(&view,4,wx)-view.camera_y/256);}
  if(stage==7 && tick==64){ht_joint c=ht_papers_burn();assert(p.hand[1].x==c.x-view.camera/256 && p.hand[1].y==c.y-view.camera_y/256);}
 }
 for(int native=0;native<2;++native){reset();action();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned stage=0;stage<=14;++stage){ht_papers_state s={true,false,true,true,(uint8_t)stage,48};ht_papers_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(bits,120);ht_papers_study_render(&ht,&s);assert(!memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && !memcmp(&frozen,&ht,sizeof(ht)));}
  const unsigned pairs[][2]={{2,4},{4,6},{6,8},{8,10},{10,12}};
  for(unsigned n=0;n<5;++n){ht_papers_state s={true,false,true,true,(uint8_t)pairs[n][0],0};ht_papers_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(mono,120);s.stage=pairs[n][1];ht_papers_study_render(&ht,&s);ht_pack_mono(bits,120);assert(memcmp(copy,ht_scene,native?HT_NATIVE_SCALE*HT_NATIVE_W*220:HT_W*220) && memcmp(bits,mono,HT_NATIVE_SCALE*HT_NATIVE_W*220/8));}
 }
 reset();ht_game g=ht;g.grounded=false;assert(!ht_papers_near(&g));g=ht;g.traversal.mode=HT_LADDER;assert(!ht_papers_near(&g));g=ht;g.x-=50*256;assert(!ht_papers_near(&g));g=ht;g.door_stage=1;assert(!ht_papers_near(&g));
 reset();action();host_exit=true;input(0,8);assert(quitting);
 reset();action();input(0,8);cancel();input(0,8);assert(!ht.packet);
 for(unsigned i=0;i<30000 && ht.x<(HT_GOAL-15)*256;++i)walk_route_tick();
 assert(ht.level==8 && !ht.deaths && ht.puzzle.solved && ht.puzzle.opening==48 && ht.puzzle.stage==1 && !ht.packet && !ht.scarf && !ht.verdict && !ht.verdict_read && !ht_weather_target(&ht));
 free(mono);free(bits);free(copy);free(memory);puts("Ridge papers: reachable page25, deliberate local papers/dog/burn/crumbs, optional packet paths, contacts, controls/read guards, immutable gray/mono and original isolation/chime route PASS");return 0;
}
