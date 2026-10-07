/* Optional mountain shelter: contacts, clocks, interruption and render copies. */
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
static void reset(void){
 memset(&ht,0,sizeof(ht));ht.level=8;ht_select_level(8);ht_spawn(true);
 ht_shelter=(ht_shelter_state){0};ht_counts=(ht_counts_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source==0?&gamepad:NULL;hid_pad=source==1?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;for(unsigned i=0;i<20000 && !ht_shelter_near(&ht);++i)walk_route_tick();
 assert(ht.level==8 && !ht.deaths && ht_shelter_near(&ht));
 ht_input(1);input(0,8);
}
static void pause_input(void){if(source<2)press(source?64:256);else {mapped=T5_APP_BUTTON_DOWN;input(0,8);mapped=0;input(0,8);}}
static void journal(void){if(source<2)press(source?128:512);else {mapped=T5_APP_BUTTON_CONFIRM;input(0,8);mapped=0;input(0,8);}}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*mono=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&mono);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 for(source=0;source<3;++source){
  reset();ht_game frozen=ht;action();assert(ht_shelter.active && !reading && !ht_shelter.stage && !memcmp(&ht,&frozen,sizeof(ht)));
  input(0,8);ticks(160);assert(!ht_shelter.stage);
  for(unsigned stage=0;stage<14;stage+=2){
   action();input(0,8);ticks(12);assert(ht_shelter.stage==stage+1);
   action();input(0,2);ticks(4);input(0,8);assert(ht_shelter.stage==stage+1);
   pause_input();ht_shelter_state stopped=ht_shelter;ticks(60);action();assert(!memcmp(&ht_shelter,&stopped,sizeof(stopped)));pause_input();input(0,8);
   if(source<2){
    journal();assert(reading && ht_journal_index);stopped=ht_shelter;ticks(60);assert(!memcmp(&ht_shelter,&stopped,sizeof(stopped)));
    journal();input(0,8);assert(!reading);
    pad_failure=true;input(0,8);stopped=ht_shelter;ticks(30);assert(!memcmp(&ht_shelter,&stopped,sizeof(stopped)));
    pad_failure=false;input(source?2:1,2);ticks(30);assert(ht_input_rearm && !memcmp(&ht_shelter,&stopped,sizeof(stopped)));input(0,8);
   }
   for(unsigned n=0;n<360 && ht_shelter.stage==stage+1;++n)ticks(1);
   assert(ht_shelter.stage==stage+2 && !ht_shelter.tick && !memcmp(&ht,&frozen,sizeof(ht)));ticks(100);assert(ht_shelter.stage==stage+2);
  }
  action();assert(!ht_shelter.active && !memcmp(&ht,&frozen,sizeof(ht)));
  /* Return requires neutral; the held A cannot immediately re-enter. */
  if(source<2){input(source?2:1,8);assert(!ht_shelter.active && ht_input_rearm);}input(0,8);
  for(unsigned stage=0;stage<=14;++stage){reset();action();ht_shelter.stage=stage;ht_shelter.tick=20;frozen=ht;input(0,8);cancel();assert(!ht_shelter.active && !memcmp(&ht,&frozen,sizeof(ht)));input(0,8);action();assert(ht_shelter.active && !ht_shelter.stage && !ht_shelter.tick);cancel();}
  reset();action();host_exit=true;input(0,8);assert(quitting);
 }
 source=0;reset();ht_game view=ht;
 /* Contact admission uses the actual opening support; no remote/air entry. */
 assert(HT_SHELTER_BUNK_LEFT>ht_level_land(8)[0].left && HT_SHELTER_DOOR_X+18<ht_level_land(8)[0].right);
 view.grounded=false;assert(!ht_shelter_near(&view));view=ht;view.y+=4*256;assert(!ht_shelter_near(&view));view=ht;view.traversal.mode=HT_ROPE;assert(!ht_shelter_near(&view));view=ht;view.x=(HT_SHELTER_X+21)*256;assert(!ht_shelter_near(&view));view=ht;
 for(unsigned stage=0;stage<=14;++stage)for(unsigned tick=0;tick<=(stage==5?320u:160u);++tick){
  ht_shelter_state s={true,false,(uint8_t)stage,(uint16_t)tick};ht_person_pose p=ht_shelter_pose(&view,&s);
  for(int i=0;i<2;++i){int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;assert(dx*dx+dy*dy<=144);}
  if(stage>=2 && stage<=6){for(int i=0;i<2;++i){int x=p.foot[i].x+view.camera/256;assert(x>=HT_SHELTER_BUNK_LEFT && x<=HT_SHELTER_BUNK_RIGHT && p.foot[i].y+view.camera_y/256==ht_shelter_bunk_top());}}
  if(stage==8 || stage==9 || stage==10){ht_joint c=ht_shelter_basin();assert(p.hand[1].x+view.camera/256==c.x && p.hand[1].y+view.camera_y/256==c.y);}
  if(stage>=8 && stage!=13){for(int i=0;i<2;++i){int x=p.foot[i].x+view.camera/256;assert(p.foot[i].y+view.camera_y/256==ht_surface_at(&view,0,x));}}
  if(stage==9 && tick>=32){ht_joint h=ht_shelter_ice_hand(&s),c=ht_shelter_ice_contact();assert(h.x==c.x && h.y==c.y && ht_shelter_ice_broken(&s));}
  if(stage==12 || (stage==11 && tick>=80)){ht_joint h=ht_shelter_ice_hand(&s);assert(h.x==323 && h.y==44);}
 }
 for(unsigned stage=7;stage<=13;stage+=6)for(unsigned tick=80;tick<120;++tick){ht_shelter_state s={true,false,(uint8_t)stage,(uint16_t)tick};ht_person_pose p=ht_shelter_pose(&view,&s);bool grounded_foot=false;for(int i=0;i<2;++i){int x=p.foot[i].x+view.camera/256;int clearance=ht_surface_at(&view,0,x)-p.foot[i].y-view.camera_y/256;assert(clearance>=0 && clearance<=3);grounded_foot|=!clearance;}assert(grounded_foot);}
 for(unsigned tick=16;tick<112;++tick){if(tick>=48 && tick<80)continue;ht_shelter_state s={true,false,3,(uint16_t)tick};unsigned target=tick<64?0:1;ht_joint cuff=ht_shelter_stocking_cuff(&s,target),pull=ht_shelter_stocking_hand(&s,1-target);assert(pull.x-6==cuff.x && pull.y-13==cuff.y);}
 for(unsigned mode=0;mode<HT_CAMERA_MODES;++mode){int native=mode==HT_CAMERA_NATIVE;reset();action();ht_camera_mode=mode;ht_game frozen=ht;
  /* Pixel-space contact regression: debug camera modes must scale the local
   * body exactly like its room, including the full-native foreground path. */
  ht_shelter_state idle={true,false,0,0};ht_shelter_study_render(&ht,&idle);
  ht_game framed=ht;framed.camera=(HT_SHELTER_X+16-240)*256;framed.camera_y=(ht_shelter_floor()-177)*256;
  framed.intimacy=mode==HT_CAMERA_NO_ZOOM || mode==HT_CAMERA_OFF?0:256;framed.vista=framed.drop_zoom=0;framed.rotation_phase=framed.sway_phase=0;
  ht_frame_camera(&framed,native?(unsigned)HT_CAMERA_BASELINE:mode);ht_person_pose body=ht_shelter_pose(&framed,&idle);
  int scale=mode==HT_CAMERA_NO_ZOOM || mode==HT_CAMERA_OFF?256:384;
  int hx=240+(body.shoulder.x-240)*scale/256,hy=135+(body.shoulder.y-5-135)*scale/256;
  assert(ht_scene[(hy*(native?2:1))*(native?HT_NATIVE_W:HT_W)+hx*(native?2:1)]==250);
  for(unsigned stage=0;stage<=14;++stage){ht_shelter_state s={true,false,(uint8_t)stage,48};ht_shelter_state snapshot=s;ht_shelter_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_shelter_study_render(&ht,&s);assert(!memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && !memcmp(&ht,&frozen,sizeof(ht)) && !memcmp(&s,&snapshot,sizeof(s)));}
  const unsigned pairs[][4]={{0,0,2,0},{3,16,4,0},{4,0,5,128},{5,0,6,0},{8,0,10,0},{10,0,12,0},{12,0,14,0}};
  for(unsigned n=0;n<sizeof(pairs)/sizeof(pairs[0]);++n){ht_shelter_state s={true,false,(uint8_t)pairs[n][0],(uint16_t)pairs[n][1]};ht_shelter_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(mono,120);s.stage=pairs[n][2];s.tick=pairs[n][3];ht_shelter_study_render(&ht,&s);ht_pack_mono(bits,120);assert(memcmp(copy,ht_scene,native?HT_NATIVE_W*452:HT_W*226) && memcmp(bits,mono,HT_NATIVE_W*452/8));}
 }
 free(mono);free(bits);free(copy);free(memory);puts("Shelter: supported bunk/basin/cloth contacts, bounded holds and dawn cutscene, HID/XInput/mapped pause/cancel/retry, journal/fault freeze, exact live-state return and gray/mono snapshot rendering PASS");return 0;
}
