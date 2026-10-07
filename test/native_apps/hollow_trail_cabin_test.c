/* Physical signal-cabin evidence through the real rail route. */
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
 memset(&ht_cabin,0,sizeof(ht_cabin));ht_carriage.active=false;
 ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;
 /* Reach the original evidence through the real shunting/trestle route. */
 for(unsigned i=0;i<18000 && ht.level==3 && ht.x<1815*256;++i)walk_route_tick();
 assert(ht.level==3 && !ht.deaths);ht_input(1);input(0,8);
 for(unsigned i=0;i<4000 && (abs(ht.x/256-ht_cabin_x())>3 || !ht.grounded);++i){
  input(0,abs(ht.x/256-ht_cabin_x())<=3?8:ht.x/256<ht_cabin_x()?2:6);ticks(1);
 }
 input(0,8);assert(ht_cabin_near(&ht));
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
 assert(memory&&copy&&bits);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 for(source=0;source<2;++source){
  reset();action();ht_game frozen=ht;assert(ht_cabin.active && !reading);input(0,8);
  action();input(0,8);ticks(40);unsigned tick=ht_cabin.tick;
  press(source?64:256);assert(ht_cabin.paused);ticks(60);assert(ht_cabin.tick==tick);action();assert(ht_cabin.tick==tick);press(source?64:256);
  press(source?128:512);assert(reading);ticks(60);assert(ht_cabin.tick==tick);present();press(source?128:512);input(0,8);assert(!reading);
  pad_failure=true;input(0,8);ticks(30);assert(ht_cabin.tick==tick);pad_failure=false;input(0,8);ticks(80);
  assert(ht_cabin.stage==2);unsigned rev=scene_revision;ticks(60);assert(scene_revision==rev);
  input(0,2);ticks(32);assert(ht_cabin.tilt==16);input(0,6);ticks(40);assert(ht_cabin.tilt==-16);input(0,8);
  action();input(0,8);ticks(168);assert(ht_cabin.stage==4);rev=scene_revision;ticks(60);assert(scene_revision==rev);
  action();input(0,8);ticks(128);assert(ht_cabin.stage==6 && !memcmp(&ht,&frozen,sizeof(ht)));
  press(source?64:256);action();assert(ht_cabin.active && !reading);press(source?64:256);
  action();assert(!ht_cabin.active && reading && !memcmp(&ht,&frozen,sizeof(ht)));present();press(source?128:512);input(0,8);assert(!reading);
  action();assert(ht_cabin.active);cancel();assert(!ht_cabin.active && ht.evidence==frozen.evidence);
  input(0,8);action();host_exit=true;input(0,8);assert(quitting);
  for(unsigned stage=0;stage<=6;++stage){reset();action();ht_cabin.stage=stage;ht_cabin.tick=20;frozen=ht;cancel();assert(!ht_cabin.active && !memcmp(&ht,&frozen,sizeof(ht)));}
 }
 source=0;reset();ht_game game=ht;game.camera=game.camera_y=0;
 for(unsigned stage=1;stage<=5;stage+=2)for(unsigned tick=0;tick<=(stage==5?128:72);++tick){
  ht_cabin_state s={true,false,0,0,(uint8_t)stage,(uint16_t)tick};ht_person_pose p=ht_cabin_pose(&game,&s);reach(&p,stage*1000+tick);
  if((stage==1 || stage==3) && tick>=64){assert(p.hand[0].x==ht_cabin_x()+(stage==1?-24:12));assert(p.hand[0].y==ht_cabin_floor()-(stage==1?29:27));}
 }
 for(int native=0;native<2;++native){
  reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned stage=0;stage<=6;++stage){ht_cabin_state state={true,false,0,16,(uint8_t)stage,stage==3?120:0};
   ht_cabin_study_render(&frozen,&state);int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,120);
   ht_cabin_study_render(&frozen,&state);assert(!memcmp(copy,ht_scene,bytes) && !memcmp(&ht,&frozen,sizeof(ht)));
  }
 }
 /* The pressure question appears within the paper, independently of hands or captions. */
 ht_camera_mode=HT_CAMERA_BASELINE;reset();ht_cabin_state map={true,false,0,0,2,0};
 ht_cabin_study_render(&ht,&map);ht_pack_mono(bits,120);memcpy(copy,bits,HT_NATIVE_PIXELS/8);
 map.tilt=16;ht_cabin_study_render(&ht,&map);ht_pack_mono(bits,120);unsigned changed=0;
 for(int y=306;y<358;++y)for(int x=378;x<544;++x)changed+=((copy[y*120+x/8]^bits[y*120+x/8])&(1u<<(7-(x&7))))!=0;
 assert(changed>100);
 /* Each of five separated long legs and the punctured eye survives the packed output. */
 ht_cabin_state dog={true,false,0,0,4,0};ht_cabin_study_render(&ht,&dog);ht_pack_mono(bits,120);
 for(int n=0;n<5;++n){unsigned ink=0;for(int y=292;y<330;++y)for(int x=(195+n*18)*2-2;x<(195+n*18)*2+10;++x)ink+=(bits[y*120+x/8]>>(7-(x&7)))&1u;assert(ink>20);}
 assert(ht_scene[105*480+294]<40 && ht_scene[105*480+297]>200);
 free(bits);free(copy);free(memory);
 puts("Signal cabin: actual rail route, map tilt, five-legged paper grip, reachable contacts, frozen puzzle, HID/XInput pause/archive/fault/cancel/retry/exit PASS");
}
