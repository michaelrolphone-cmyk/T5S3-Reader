/* Food and water reached from the actual first-house spawn, jumping the crate. */
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
 memset(&ht_food,0,sizeof(ht_food));ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=jump_down=pause_down=false;held=previous=mapped=0;
 ht_pad_owned=ht_input_rearm=false;ht_pad_source=-1;simulation_started=false;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 ht_input(1);input(0,8);
 bool jumped=false;
 for(unsigned i=0;i<2000 && (abs(ht.x/256-HT_FOOD_X)>2 || !ht.grounded);++i){
  uint32_t jump=0;if(!jumped && ht.x/256>=294 && ht.grounded){jump=source?1:2;jumped=true;}
  input(jump,abs(ht.x/256-HT_FOOD_X)<=2?8:ht.x/256<HT_FOOD_X?2:6);ticks(1);
 }
 input(0,8);assert(ht_food_near(&ht));
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
  reset();assert(ht.traversal.crate_x==330*256 && !ht.evidence && !ht.deaths);action();ht_game frozen=ht;
  assert(ht_food.active && !reading);input(0,8);action();input(0,8);ticks(64);unsigned tick=ht_food.tick;
  press(source?64:256);assert(ht_food.paused);ticks(60);assert(ht_food.tick==tick);action();assert(ht_food.tick==tick);press(source?64:256);
  press(source?128:512);assert(reading);ticks(60);assert(ht_food.tick==tick);present();press(source?128:512);input(0,8);assert(!reading);
  pad_failure=true;input(0,8);ticks(30);assert(ht_food.tick==tick);pad_failure=false;input(source?2:1,2);ticks(30);assert(ht_food.tick==tick && ht_input_rearm);input(0,8);
  for(unsigned n=0;n<192 && ht_food.stage==1;++n){ticks(1);}
  assert(ht_food.stage==2);action();input(0,8);assert(ht_food.stage==2);ticks(96);assert(ht_food.stage==3);
  action();input(0,8);ticks(128);assert(ht_food.stage==5);
  action();input(0,8);ticks(160);assert(ht_food.stage==7);
  action();input(0,8);ticks(160);assert(ht_food.stage==9);
  action();input(0,8);ticks(256);assert(ht_food.stage==11);
  action();input(0,8);assert(ht_food.stage==12);unsigned rev=scene_revision;ticks(600);assert(scene_revision==rev);
  action();input(0,8);ticks(320);assert(!ht_food.active && ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
  input(source?2:1,2);ticks(3);assert(ht_input_rearm && !held && !ht_food.active);input(0,8);action();assert(ht_food.active);cancel();assert(!ht_food.active);
  input(0,8);action();host_exit=true;input(0,8);assert(quitting);
  for(unsigned stage=0;stage<=13;++stage){reset();action();ht_food.stage=stage;ht_food.tick=20;frozen=ht;cancel();assert(!ht_food.active && !memcmp(&ht,&frozen,sizeof(ht)));input(0,8);action();assert(ht_food.active && !ht_food.stage && !ht_food.tick);}
 }
 source=0;reset();ht_game game=ht;game.camera=game.camera_y=0;
 for(unsigned stage=0;stage<=13;++stage)for(unsigned tick=0;tick<=(ht_food_limit(stage)?ht_food_limit(stage):1);++tick){ht_food_state s={true,false,(uint8_t)stage,(uint16_t)tick,0};ht_person_pose p=ht_food_pose(&game,&s);reach(&p,stage*1000+tick);}
 /* The upper biscuit is removed at the same contact as the taking hand. */
 ht_food_state take={true,false,8,64,0};ht_person_pose taken=ht_food_pose(&game,&take);assert(taken.hand[0].x==HT_FOOD_X+63 && taken.hand[0].y==ht_food_floor()-10);
 for(int native=0;native<2;++native){reset();ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
  for(unsigned stage=0;stage<=13;++stage){ht_food_state state={true,false,(uint8_t)stage,64,248};ht_food_study_render(&frozen,&state);int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;memcpy(copy,ht_scene,bytes);ht_pack_mono(mono,120);ht_food_study_render(&frozen,&state);ht_pack_mono(bits,120);assert(!memcmp(copy,ht_scene,bytes) && !memcmp(mono,bits,HT_NATIVE_PIXELS/8) && !memcmp(&ht,&frozen,sizeof(ht)));}
  ht_food_state still={true,false,0,0,239};ht_food_study_render(&frozen,&still);ht_pack_mono(mono,120);still.ambient=248;ht_food_study_render(&frozen,&still);ht_pack_mono(bits,120);unsigned change=0;for(unsigned n=0;n<120*440;++n){unsigned d=mono[n]^bits[n];for(int b=0;b<8;++b)change+=(d>>b)&1u;}assert(change>3);
 }
 reset();ht_game frozen=ht;frozen.traversal.mode=HT_CRATE;assert(!ht_food_near(&frozen));frozen=ht;frozen.grounded=false;assert(!ht_food_near(&frozen));frozen=ht;frozen.y-=80*256;assert(!ht_food_near(&frozen));frozen=ht;frozen.traversal.crate_x=ht.x;assert(!ht_food_near(&frozen));frozen.traversal.crate_x=452*256;assert(!ht_food_near(&frozen));
 /* Continue the unchanged crate-step route through all three original evidence
  * sites and the two-stage mirror/vent puzzle after the optional scene. */
 walk_level=999;for(unsigned n=0;n<30000 && ht.level==6;++n)walk_route_tick();assert(ht.level==7 && !ht.deaths && walk_mechanics&2 && ht.evidence==((uint32_t)7<<18));
 free(mono);free(bits);free(copy);free(memory);puts("Food/drink: real crate jump, deliberate drink/wait/drink, one biscuit/recovery/list/closure, bounded contacts, both rasters and slow drop, frozen evidence/route, HID/XInput interruptions/replay/exit and unchanged mirror route PASS");
}
