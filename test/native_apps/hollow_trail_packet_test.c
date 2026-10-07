/* Optional packet acquisition, physical contacts and persistence contracts. */
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
/* Focused ownership fixtures use the real interaction coordinates and puzzle
 * operations. Full route traversal is covered by the existing chapter tests. */
static void reset_at(unsigned level,bool solved){
 memset(&ht,0,sizeof(ht));ht.level=level;ht_select_level(level);ht_spawn(true);
 ht_cabin=(ht_cabin_state){0};ht_food=(ht_food_state){0};ht_drawing=(ht_drawing_state){0};
 ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;
 reading=paused=quitting=loading=debug_jump=debug_select=jump_down=pause_down=false;
 held=previous=mapped=0;ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;simulation_started=false;simulation_accumulator=0;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};
 pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 ht.x=(level==3?ht_cabin_x():HT_FOOD_X)*256;ht.y=(level==3?ht_cabin_floor():ht_food_floor())*256;
 if(solved)for(const char *move=walk_solutions[level];*move;++move)ht_puzzle_operate(&ht.puzzle,level,(unsigned)(*move-'0'));
 assert(ht.puzzle.solved==solved);ht_input(1);input(0,8);
}
static void cabin_until(unsigned stage){input(0,8);for(unsigned n=0;n<240 && ht_cabin.stage<stage;++n)ticks(1);assert(ht_cabin.stage==stage);}
static void food_until(unsigned stage){input(0,8);for(unsigned n=0;n<400 && ht_food.stage<stage;++n)ticks(1);assert(ht_food.stage==stage);}
static void packet_only(ht_game before,uint8_t packet){before.packet=packet;if(memcmp(&before,&ht,sizeof(ht)))fprintf(stderr,"packet state level=%u cabin=%u food=%u packet=%u tick %u/%u x %d/%d y %d/%d\n",ht.level,ht_cabin.stage,ht_food.stage,ht.packet,before.ticks,ht.ticks,before.x,ht.x,before.y,ht.y);assert(!memcmp(&before,&ht,sizeof(ht)));}
static void pause_input(void){press(source?64:256);}
static void journal_input(void){press(source?128:512);}
static void cabin_ownership(void){
 reset_at(3,false);action();assert(ht_cabin.active && !ht.packet);
 /* Reading, unwrapping, and rewrapping never grant possession. */
 for(unsigned stage=2;stage<=6;stage+=2){action();cabin_until(stage);assert(!ht.packet);}
 action();assert(reading && !ht_cabin.active && !ht.packet);
 reset_at(3,true);action();assert(ht_cabin.stage==6 && !ht.packet);ht_game frozen=ht;
 cancel();assert(!ht_cabin.active && !ht.packet);action();action();assert(ht_cabin.stage==7);input(0,8);ticks(72);cancel();packet_only(frozen,0);
 action();action();input(0,8);ticks(20);pause_input();ht_cabin_state stopped=ht_cabin;ticks(80);action();assert(!memcmp(&stopped,&ht_cabin,sizeof(stopped)) && !ht.packet);
 journal_input();assert(reading && journal_page==10);ticks(80);assert(!memcmp(&stopped,&ht_cabin,sizeof(stopped)));present();journal_input();input(0,8);assert(!reading && ht_cabin.paused);pause_input();
 cabin_until(8);packet_only(frozen,HT_PACKET_DOG);action();assert(!ht_cabin.active);
 action();assert(ht_cabin.stage==8);ticks(250);packet_only(frozen,HT_PACKET_DOG);action();
 /* The owned paper cannot reappear on the lever, even on re-entry. */
 for(unsigned native=0;native<2;++native){
  ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game view=ht;view.camera=(ht_cabin_x()-236)*256;view.camera_y=(ht_cabin_floor()-192)*256;
  ht_frame_camera(&view,HT_CAMERA_BASELINE);memset(ht_scene,0,HT_NATIVE_PIXELS);ht_signal_cabin_room(&view);
  uint8_t *copy=malloc(HT_NATIVE_PIXELS);assert(copy);memcpy(copy,ht_scene,HT_NATIVE_PIXELS);
  view.packet=0;memset(ht_scene,0,HT_NATIVE_PIXELS);ht_signal_cabin_room(&view);assert(memcmp(copy,ht_scene,native?HT_NATIVE_PIXELS:HT_PIXELS));free(copy);
 }
}
static void provision_ownership(void){
 reset_at(6,false);ht_food_begin();
 for(unsigned n=0;n<3000 && ht_food.active;++n){if(!ht_food_limit(ht_food.stage))assert(ht_food_action(&ht_food));(void)ht_food_step(&ht_food);}
 assert(!ht_food.active && !ht.packet); /* The first eaten biscuit is not road stock. */
 reset_at(6,true);assert(ht_food_near(&ht));action();assert(ht_food.stage==14);ht_game frozen=ht;cancel();packet_only(frozen,0);
 action();action();food_until(16);assert(!ht.packet);action();input(0,8);ticks(100);cancel();packet_only(frozen,0);
 action();action();food_until(16);action();food_until(18);assert(!ht.packet);
 action();input(0,8);ticks(160);assert(ht_food.stage==19 && !ht.packet);pause_input();ht_food_state stopped=ht_food;ticks(80);action();assert(!memcmp(&stopped,&ht_food,sizeof(stopped)) && !ht.packet);
 journal_input();assert(reading);ticks(80);assert(!memcmp(&stopped,&ht_food,sizeof(stopped)));journal_input();input(0,8);assert(!reading && ht_food.paused);pause_input();
 food_until(20);packet_only(frozen,HT_PACKET_FOOD);cancel();action();assert(ht_food.stage==14);action();food_until(20);packet_only(frozen,HT_PACKET_FOOD);
 action();input(0,8);for(unsigned n=0;n<322 && ht_food.active;++n)ticks(1);assert(!ht_food.active);packet_only(frozen,HT_PACKET_FOOD);
}
static void drawing_without_dog(void){
 reset_at(6,false);ht.x=ht_drawing_x()*256;ht.y=ht_drawing_floor()*256;assert(ht_drawing_near(&ht));
 action();assert(ht_drawing.active);action();input(0,8);ticks(98);assert(ht_drawing.stage==2);action();assert(ht_drawing.stage==7);input(0,8);ticks(98);assert(ht_drawing.stage==8 && !ht.packet);
 /* Rendering honors its snapshot, not the mutable global ownership. */
 ht_drawing_state state={true,false,0,0,4,0};ht_game view=ht;view.packet=HT_PACKET_DOG;
 for(unsigned native=0;native<2;++native){ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;size_t bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;uint8_t *copy=malloc(bytes);assert(copy);
  ht_drawing_study_render(&view,&state);memcpy(copy,ht_scene,bytes);view.packet=0;ht_drawing_study_render(&view,&state);assert(memcmp(copy,ht_scene,bytes));view.packet=HT_PACKET_DOG;free(copy);
 }
}
static void reach(const ht_person_pose *p,unsigned stage,unsigned tick){
 for(unsigned i=0;i<2;++i){int dx=p->hand[i].x-p->shoulder.x,dy=p->hand[i].y-p->shoulder.y;
  if(dx*dx+dy*dy>144)fprintf(stderr,"packet arm stage=%u tick=%u side=%u dx=%d dy=%d\n",stage,tick,i,dx,dy);
  assert(dx*dx+dy*dy<=144);
 }
}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=calloc(1,HT_NATIVE_PIXELS/8),*copy=malloc(HT_NATIVE_PIXELS);assert(memory && bits && copy);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 for(source=0;source<2;++source){cabin_ownership();provision_ownership();drawing_without_dog();}
 source=0;reset_at(3,true);ht_game view=ht;view.camera=view.camera_y=0;
 for(unsigned t=0;t<=192;++t){ht_cabin_state s={true,false,0,0,7,(uint16_t)t};ht_person_pose p=ht_cabin_pose(&view,&s);reach(&p,7,t);}
 reset_at(6,true);view=ht;view.camera=view.camera_y=0;
 for(unsigned stage=14;stage<=21;++stage)for(unsigned t=0;t<=(ht_food_limit(stage)?ht_food_limit(stage):1);++t){ht_food_state s={true,false,(uint8_t)stage,(uint16_t)t,0};ht_person_pose p=ht_food_pose(&view,&s);reach(&p,stage,t);}
 for(unsigned native=0;native<2;++native){ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;size_t bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
  for(unsigned stage=14;stage<=21;++stage){ht_food_state s={true,false,(uint8_t)stage,160,0};ht_game frozen=ht;ht_food_study_render(&ht,&s);memcpy(copy,ht_scene,bytes);ht_food_study_render(&ht,&s);assert(!memcmp(copy,ht_scene,bytes) && !memcmp(&ht,&frozen,sizeof(ht)));}
 }
 /* Discoveries and chapter jumps cannot materialize unearned objects. */
 ht.packet=0;ht.evidence=0x3fffffff;ht.level=8;ht_spawn(true);assert(!ht.packet);
 ht.packet=HT_PACKET_DOG|HT_PACKET_FOOD|HT_PACKET_CRUMBS;ht_spawn(false);assert(ht.packet==7);ht.level=9;ht_spawn(true);assert(ht.packet==7);
 ht.level=0;ht_spawn(true);assert(!ht.packet && ht.evidence==0x3fffffff);
 free(copy);free(bits);free(memory);puts("Packet ownership: explicit cabin/cupboard transfers, decline/cancel/pause/journal/replay, conditional garden dog, snapshot rendering, bounded hands, retained chapter/death ownership and new-journey reset PASS");
}
