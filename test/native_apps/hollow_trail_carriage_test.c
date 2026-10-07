/* Optional carriage entry and rest through real rail-route input. */
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
 memset(&ht,0,sizeof(ht));ht.level=3;ht_select_level(3);ht_spawn(true);
 ht_carriage=(ht_carriage_state){0};ht_hoist=(ht_hoist_state){0};ht_partition=(ht_partition_state){0};
 ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 ht_input(1);input(0,8);
 for(unsigned i=0;i<2000 && (abs(ht.x/256-(HT_CARRIAGE_X-16))>2 || !ht.grounded);++i){
  input(0,ht.x/256<HT_CARRIAGE_X-16?2:6);ticks(1);
 }
 input(0,8);assert(ht_carriage_near(&ht));
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
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits);ht_bind(memory);ht_bind_native(memory);
 for(source=0;source<2;++source){
  reset();action();ht_game frozen=ht;assert(ht_carriage.active);input(0,8);ticks(80);unsigned tick=ht_carriage.tick;
  press(source?64:256);assert(ht_carriage.paused);ticks(60);assert(ht_carriage.tick==tick);press(source?64:256);
  press(source?128:512);assert(reading && ht_journal_index);ticks(60);assert(ht_carriage.tick==tick);press(source?128:512);input(0,8);assert(!reading);
  pad_failure=true;input(0,8);ticks(30);assert(ht_carriage.tick==tick);pad_failure=false;input(0,8);ticks(200);
  assert(ht_carriage.stage==1);unsigned rev=scene_revision;ticks(60);assert(scene_revision==rev);
  action();input(0,8);ticks(224);assert(ht_carriage.stage==7);ticks(HT_CARRIAGE_REFLECTION_TICKS);assert(ht_carriage.stage==3);rev=scene_revision;ticks(60);assert(scene_revision==rev);
  action();input(0,8);assert(ht_carriage.stage==8);
  ticks(HT_CARRIAGE_DREAM_TICKS);assert(ht_carriage.stage==9);rev=scene_revision;ticks(120);assert(scene_revision==rev);
  action();input(0,8);ticks(288);assert(ht_carriage.stage==5);rev=scene_revision;ticks(60);assert(scene_revision==rev && !memcmp(&ht,&frozen,sizeof(ht)));
  action();input(0,8);ticks(344);assert(!ht_carriage.active && ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
  input(source?2:1,2);ticks(3);assert(!ht_carriage.active && ht_input_rearm && held==0);
  input(0,8);action();assert(ht_carriage.active);cancel();assert(!ht_carriage.active && ht.evidence==frozen.evidence);
  input(0,8);action();host_exit=true;input(0,8);assert(quitting);
  for(unsigned stage=7;stage<=9;++stage){
   reset();action();ht_carriage.stage=stage;ht_carriage.tick=stage==9?0:40;frozen=ht;
   unsigned at=ht_carriage.tick;press(source?64:256);ticks(100);assert(ht_carriage.paused && ht_carriage.tick==at);
   press(source?64:256);press(source?128:512);ticks(100);assert(reading && ht_carriage.tick==at);
   press(source?128:512);input(0,8);assert(!reading);
   pad_failure=true;input(0,8);ticks(40);assert(ht_carriage.tick==at);
   pad_failure=false;input(source?2:1,2);ticks(3);assert(ht_input_rearm && ht_carriage.tick==at);
   input(0,8);ticks(1);assert(!ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
   if(stage<9){unsigned st=ht_carriage.stage;action();assert(ht_carriage.stage==st);}
   host_exit=true;input(0,8);assert(quitting);
  }
  for(unsigned stage=0;stage<=9;++stage){reset();action();ht_carriage.stage=stage;ht_carriage.tick=stage==1 || stage==3 || stage==5 || stage==9?0:40;frozen=ht;cancel();assert(!ht_carriage.active && !memcmp(&ht,&frozen,sizeof(ht)));}
 }
 source=0;reset();ht_game game=ht;game.camera=game.camera_y=0;
 for(unsigned tick=0;tick<=184;++tick){
  ht_person_pose p=ht_carriage_entry_pose(&game,tick);reach(&p,tick);
  int floor=ht_carriage_ground();
  bool hip_wall=p.hip.x>=HT_CARRIAGE_X && p.hip.x<HT_CARRIAGE_X+16 && p.hip.y>floor-26 && p.hip.y<floor-12;
  if(hip_wall)fprintf(stderr,"hip wall tick=%u at=%d,%d\n",tick,p.hip.x,p.hip.y);
  assert(!hip_wall);
  for(int i=0;i<2;++i){int x=p.foot[i].x,y=p.foot[i].y,f=ht_carriage_ground();
   bool wall=x>=HT_CARRIAGE_X && x<HT_CARRIAGE_X+16 && y>f-26 && y<f-12;
   if(wall)fprintf(stderr,"wall tick=%u foot=%d at=%d,%d\n",tick,i,x,y);
   assert(!wall);
   bool block=x>=HT_CARRIAGE_X-6 && x<HT_CARRIAGE_X && y>f-8 && y<f;
   if(block)fprintf(stderr,"block tick=%u foot=%d at=%d,%d\n",tick,i,x,y);
   assert(!block);
  }
 }
 for(unsigned tick=0;tick<=224;++tick){ht_person_pose p=ht_carriage_rest_pose(&game,tick,false);reach(&p,1000+tick);}
 ht_person_pose wake=ht_carriage_rest_pose(&game,224,true);reach(&wake,2000);
 /* The finger lands in an unmarked cell, while the traveller supports the
  * page at its lower-left edge. Both render contacts use these endpoints. */
 ht_native_active=false;ht_world_scale=256;memset(ht_scene,0,HT_PIXELS);ht_carriage_account(0,0);
 ht_joint blank=ht_carriage_blank_place(),grip=ht_carriage_account_grip();
 assert(ht_scene[blank.y*HT_W+blank.x]==31);
 assert(ht_scene[grip.y*HT_W+grip.x]!=0); /* paper or its ruling, never outside */
 for(unsigned tick=0;tick<=HT_CARRIAGE_DREAM_TICKS;++tick){
  ht_joint finger=ht_carriage_dream_finger(tick);int dx=finger.x+5-268,dy=finger.y-1-163;
  assert(dx*dx+dy*dy<=52*52); /* elbow-to-finger stays within the drawn arm */
 }
 for(unsigned tick=608;tick<=HT_CARRIAGE_DREAM_TICKS;++tick){
  ht_joint finger=ht_carriage_dream_finger(tick),blank=ht_carriage_blank_place();assert(finger.x==blank.x && finger.y==blank.y);
 }
 for(int native=0;native<2;++native){
  reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned stage=0;stage<=9;++stage){ht_carriage_state state={true,false,(uint8_t)stage,stage==0?128:stage==2?80:0};
   ht_carriage_study_render(&frozen,&state);int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,120);
   ht_carriage_study_render(&frozen,&state);assert(!memcmp(copy,ht_scene,bytes) && !memcmp(&ht,&frozen,sizeof(ht)));
  }
 }
 reset();ht.level=2;assert(!ht_carriage_near(&ht));ht.level=3;ht.x=(HT_CARRIAGE_X+20)*256;assert(!ht_carriage_near(&ht));
 free(bits);free(copy);free(memory);
 puts("Sleeping carriage: real route entry, reachable shared timber/window contacts, no lower-wall penetration, reversible exit, boots/reflection/account-dream/held-reply/dawn, frozen route and HID/XInput interruption/retry/exit PASS");
}
