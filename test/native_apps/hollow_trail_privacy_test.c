/* Privacy reached on foot through the existing crate step and sleeping house. */
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
 memset(&ht,0,sizeof(ht));ht.level=6;ht_select_level(6);ht_spawn(true);
 ht_privacy=(ht_privacy_state){0};ht_counts=(ht_counts_state){0};ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;for(unsigned i=0;i<20000 && !ht_privacy_near(&ht);++i)walk_route_tick();
 assert(ht.level==6 && !ht.deaths && ht_privacy_near(&ht));
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
  reset();assert(ht_abs(ht.traversal.crate_x/256-452)<=3);ht_game frozen=ht;action();
  assert(ht_privacy.active && !reading && !ht_privacy.stage);input(0,8);action();input(0,8);ticks(64);unsigned tick=ht_privacy.tick;
  press(source?64:256);assert(ht_privacy.paused);ticks(60);assert(ht_privacy.tick==tick);action();assert(ht_privacy.tick==tick);press(source?64:256);
  press(source?128:512);assert(reading);ticks(60);assert(ht_privacy.tick==tick);present();press(source?128:512);input(0,8);assert(!reading);
  pad_failure=true;input(0,8);ticks(30);assert(ht_privacy.tick==tick);pad_failure=false;input(source?2:1,2);ticks(30);assert(ht_privacy.tick==tick && ht_input_rearm);input(0,8);
  for(unsigned n=0;n<128 && ht_privacy.stage==1;++n)ticks(1);
  assert(ht_privacy.stage==2);assert(!memcmp(&ht,&frozen,sizeof(ht)));
  for(unsigned stage=2;stage<=8;stage+=2){
   assert(ht_privacy.stage==stage);input(0,8);ticks(130);assert(ht_privacy.stage==stage && !ht_privacy.tick);
   action();input(0,8);ticks(ht_privacy_limit(stage+1));
  }
  assert(!ht_privacy.active && ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
  input(source?2:1,2);ticks(3);assert(ht_input_rearm && !held && !ht_privacy.active);input(0,8);action();assert(ht_privacy.active);cancel();assert(!ht_privacy.active);
  input(0,8);action();host_exit=true;input(0,8);assert(quitting);
  for(unsigned stage=0;stage<=9;++stage){
   reset();action();ht_privacy.stage=stage;ht_privacy.tick=20;frozen=ht;input(0,8);
   press(source?64:256);ht_privacy_state stopped=ht_privacy;ticks(60);action();assert(!memcmp(&ht_privacy,&stopped,sizeof(stopped)));press(source?64:256);input(0,8);
   press(source?128:512);stopped=ht_privacy;ticks(60);assert(!memcmp(&ht_privacy,&stopped,sizeof(stopped)));present();press(source?128:512);input(0,8);
   pad_failure=true;input(0,8);stopped=ht_privacy;ticks(30);assert(!memcmp(&ht_privacy,&stopped,sizeof(stopped)));pad_failure=false;input(source?2:1,2);ticks(30);assert(ht_input_rearm && !memcmp(&ht_privacy,&stopped,sizeof(stopped)));input(0,8);
   cancel();assert(!ht_privacy.active && !memcmp(&ht,&frozen,sizeof(ht)));input(0,8);action();assert(ht_privacy.active && !ht_privacy.stage && !ht_privacy.tick);cancel();
  }
 }
 source=0;reset();ht_game game=ht;game.camera=game.camera_y=0;
 /* Every transition pose has reachable hands and legs. The live contacts are
  * never moved to the shelf or to the unrelated home-memory coordinates. */
 for(unsigned stage=0;stage<=9;++stage)for(unsigned tick=0;tick<=(ht_privacy_limit(stage)?ht_privacy_limit(stage):1);++tick){ht_privacy_state s={true,false,(uint8_t)stage,(uint16_t)tick};ht_person_pose p=ht_privacy_pose(&game,&s);reach(&p,stage*1000+tick);}
 for(unsigned tick=0;tick<=224;++tick){ht_privacy_state s={true,false,5,(uint16_t)tick};ht_person_pose p=ht_privacy_towel_pose(&game,&s);reach(&p,5000+tick);ht_joint fold=ht_privacy_fold(&s);
  if(tick>=48 && tick<=192){assert(p.hand[1].x==fold.x-2 && p.hand[1].y==fold.y+6);}
  if(tick>=96 && tick<=144){assert(fold.x==p.shoulder.x-3 && fold.y==p.shoulder.y-4);}
  assert(ht_privacy_dust(&s)==(tick>=112));
 }
 for(unsigned tick=0;tick<448;++tick){ht_person_pose p=ht_privacy_memory_pose(&game,tick);reach(&p,7000+tick);unsigned t=tick%224;if(t>=96 && t<=152){assert(p.hand[1].x==249 && p.hand[1].y==129-(int)ht_privacy_pin_lift(t)*8/256);}}
 unsigned teeth=0;for(unsigned n=0;n<16;++n)teeth+=ht_privacy_comb_tooth(n);assert(teeth==13);
 for(int native=0;native<2;++native){reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned stage=0;stage<=9;++stage){ht_privacy_state state={true,false,(uint8_t)stage,104};ht_privacy_study_render(&ht,&state);assert(!memcmp(&ht,&frozen,sizeof(ht)));ht_pack_mono(bits,120);}
  /* Compare the actual exterior against the same scene without its body:
   * the screen must conceal torso/head, but both raster paths show the feet. */
  ht_game v=ht;v.camera=(HT_PRIVACY_INSIDE_X-240)*256;v.camera_y=(ht_sleep_floor()-170)*256;v.intimacy=512;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
  ht_frame_camera(&v,HT_CAMERA_BASELINE);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);ht_sleep_screen(&v);
  memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(mono,120);
  ht_privacy_state hidden={true,false,2,0};ht_person_pose feet=ht_privacy_pose(&v,&hidden);ht_person_draw(&feet);ht_sleep_screen(&v);
  unsigned changed=0;int raster=native?2:1,w=native?960:480,h=native?540:270;
  int floor=ht_project_y(ht_surface_at(&v,1,HT_PRIVACY_INSIDE_X)-v.camera_y/256)*raster;
  for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(ht_scene[y*w+x]!=copy[y*w+x]){++changed;assert(y>=floor-14*raster && y<=floor+3*raster);}
  ht_pack_mono(bits,120);assert(changed>4 && memcmp(bits,mono,HT_NATIVE_PIXELS/8));ht_world_scale=256;
  printf("privacy visible-feet native=%d gray_pixels=%u\n",native,changed);
  ht_privacy_state s={true,false,4,0};ht_privacy_study_render(&ht,&s);memcpy(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS);ht_pack_mono(mono,120);
  s.stage=6;ht_privacy_study_render(&ht,&s);ht_pack_mono(bits,120);assert(memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS) && memcmp(bits,mono,HT_NATIVE_PIXELS/8));
 }
 /* Real completion returns to the original coordinate; the original evidence
  * sites and two-stage mirror route remain reachable without doing this scene. */
 reset();action();input(0,8);for(unsigned n=0;n<2000 && ht_privacy.active;++n){if(!ht_privacy_limit(ht_privacy.stage)){action();input(0,8);}ticks(1);}assert(!ht_privacy.active);input(0,8);
 walk_level=999;for(unsigned n=0;n<30000 && ht.level==6;++n)walk_route_tick();assert(ht.level==7 && !ht.deaths && ht_evidence_found(&ht,18) && ht_evidence_found(&ht,19) && ht_evidence_found(&ht,20));
 free(mono);free(bits);free(copy);free(memory);puts("Hollow Trail privacy: grounded route, contacts, HID/XInput interruptions, frozen state, memory and both rasters PASS");return 0;
}
