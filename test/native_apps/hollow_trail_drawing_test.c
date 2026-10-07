/* Earn the two garden drawing through the authored route and real app input. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail.c"
#include "hollow_trail_route_walk.inc"

static uint32_t now,mapped;
static bool host_exit,pad_failure;
static unsigned source;
static risc_usb_gamepad_state_v1 report;
static bool fake_poll(t5_app_input_t *out,uint32_t wait) {
    now+=wait;memset(out,0,sizeof(*out));out->buttons=mapped;out->exit_requested=host_exit;return true;
}
static uint32_t clock_ms(void){return now;}
static const t5_app_api_v1 fake_app={.abi_version=1,.struct_size=sizeof(fake_app),.poll=fake_poll,.millis=clock_ms};
const t5_app_api_v1 *t5_app_get_api(uint32_t v){(void)v;return &fake_app;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v){(void)v;return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v){(void)v;return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v){(void)v;return NULL;}
static bool raw_poll(void *ctx,size_t n){(void)ctx;(void)n;return !pad_failure;}
static bool snapshot(void *ctx,risc_usb_gamepad_state_v1 *out,size_t *count){
    (void)ctx;assert(*count);*out=report;*count=1;return true;
}
static const risc_usb_gamepad_api_v1 gamepad={.api_version=1,.struct_size=sizeof(gamepad),.poll=raw_poll,.snapshot=snapshot};
static uint32_t a_button(void){return source?2u:1u;}
static void input(uint32_t buttons,unsigned hat){report.buttons=buttons;report.hat=(uint8_t)hat;ht_input(1);}
static void press(uint32_t mask){input(0,8);input(mask,8);}
static void action(void){press(a_button());}
static void cancel(void){press(source?4:8);}
static void journal(void){press(source?128:512);}
static void pause_study(void){press(source?64:256);}
static void ticks(unsigned n){for(unsigned i=0;i<n;++i)ht_advance(now+=HT_STEP_MS);}
static void present(void){ht_journal_render();assert(ht_journal_page_ready);ht_read_submitted_revision=scene_revision;}

static void reset(void){
 /* This owned-paper fixture earns the departure transfer at its real cabin.
  * The separate packet test covers declining it and the no-dog branch. */
 memset(&ht,0,sizeof(ht));ht.level=3;ht_select_level(3);ht_spawn(true);
 ht.x=ht_cabin_x()*256;ht.y=ht_cabin_floor()*256;
 for(const char *move=walk_solutions[3];*move;++move)ht_puzzle_operate(&ht.puzzle,3,(unsigned)(*move-'0'));
 assert(ht.puzzle.solved);ht_cabin_begin();assert(ht_cabin_action(&ht_cabin));
 for(unsigned n=0;n<192;++n)assert(ht_cabin_step(&ht_cabin));
 assert(ht.packet==HT_PACKET_DOG);ht_cabin.active=false;
 ht.level=6;ht_select_level(6);ht_spawn(true);ht_drawing=(ht_drawing_state){0};
 ht_counts=(ht_counts_state){0};ht_sleep=(ht_sleep_state){0};ht_partition=(ht_partition_state){0};ht_bed=(ht_bed_state){0};ht_hoist=(ht_hoist_state){0};
 ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;reading=paused=quitting=loading=debug_jump=debug_select=jump_down=pause_down=false;
 ht_schoolroom_studying=ht_signal_room_studying=false;held=previous=mapped=0;ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;ht_pad_device=0;simulation_started=false;simulation_accumulator=0;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;for(unsigned n=0;n<25000 && !ht_drawing_near(&ht);++n)walk_route_tick();assert(ht.level==6 && ht_drawing_near(&ht) && !ht.deaths);ht_input(1);input(0,8);
}
static ht_game enter(void){ht_game g=ht;action();assert(ht_drawing.active && !ht_drawing.stage && !reading);g.evidence|=1u<<20;g.scene_evidence|=4;assert(!memcmp(&g,&ht,sizeof(g)));input(0,8);return ht;}
static void unchanged(const ht_drawing_state *s){assert(ht_drawing.stage==s->stage && ht_drawing.tick==s->tick && ht_drawing.shift==s->shift);}
static void controls(void){
 reset();ht_game frozen=enter();unsigned rev=scene_revision;ticks(60);assert(scene_revision==rev);
 for(unsigned stage=0;stage<=8;++stage){
  ht_drawing.stage=(uint8_t)stage;ht_drawing.tick=20;ht_drawing.shift=15;
  pause_study();assert(ht_drawing.paused);ht_drawing_state before=ht_drawing;input(a_button(),2);ticks(40);unchanged(&before);journal();assert(reading && journal_page==20);ticks(40);unchanged(&before);journal();input(0,8);assert(!reading && ht_drawing.paused);pause_study();input(0,8);
  before=ht_drawing;pad_failure=true;input(0,2);ticks(20);unchanged(&before);pad_failure=false;input(a_button(),2);ticks(20);unchanged(&before);assert(ht_input_rearm);input(0,8);
  cancel();assert(!ht_drawing.active && ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));input(a_button(),2);assert(!ht_drawing.active);input(0,8);action();input(0,8);assert(ht_drawing.active && !ht_drawing.stage);
 }
 action();input(0,8);ticks(96);assert(ht_drawing.stage==2);
 input(0,2);ticks(240);input(0,8);assert(ht_drawing.shift==94);rev=scene_revision;ticks(60);assert(scene_revision==rev);
 input(0,6);ticks(240);input(0,8);assert(ht_drawing.shift==-20);action();input(0,8);ticks(96);assert(ht_drawing.stage==4);
 rev=scene_revision;ticks(80);assert(scene_revision==rev);action();input(0,8);ticks(96);assert(ht_drawing.stage==6);action();input(0,8);ticks(96);assert(ht_drawing.stage==8 && !memcmp(&ht,&frozen,sizeof(ht)));
 action();assert(reading && journal_page==20 && !ht.verdict_read);for(unsigned leaf=0;;++leaf){assert(leaf<64);present();if(ht_journal_next==ht_journal_length)break;input(0,8);input(0,2);}action();assert(ht_journal_index && !ht.verdict_read);journal();input(0,8);host_exit=true;input(0,8);assert(quitting);
}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8);assert(memory && copy && bits);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 for(source=0;source<2;++source)controls();
 source=0;reset();ht_game frozen=enter();
 for(unsigned stage=0;stage<=8;++stage)for(unsigned tick=0;tick<=96;++tick){ht_drawing_state s={true,false,0,0,(uint8_t)stage,(uint16_t)tick};ht_person_pose p=ht_drawing_pose(&frozen,&s);for(unsigned side=0;side<2;++side){int dx=p.hand[side].x-p.shoulder.x,dy=p.hand[side].y-p.shoulder.y;if(dx*dx+dy*dy>144)fprintf(stderr,"arm %u %u %d %d\n",stage,tick,dx,dy);assert(dx*dx+dy*dy<=144);dx=p.foot[side].x-p.hip.x;dy=p.foot[side].y-1-p.hip.y;if(dx*dx+dy*dy>225)fprintf(stderr,"leg %u %u %d %d\n",stage,tick,dx,dy);assert(dx*dx+dy*dy<=225);}}
 /* Two late child figures must stay on the paper, not wrap int8 coordinates. */
 memset(ht_scene,0,HT_NATIVE_PIXELS);ht_world_scale=256;ht_drawing_picture(30,30,true);
 for(int column=0;column<2;++column){unsigned ink=0;int cx=30+(column?158:137);for(int y=100;y<148;++y)for(int x=cx-5;x<=cx+5;++x)ink+=ht_scene[y*HT_W+x]>170;assert(ink>25);}
 for(unsigned stage=3;stage<=5;++stage)for(unsigned tick=0;tick<=96;++tick){ht_drawing_state s={true,false,0,0,(uint8_t)stage,(uint16_t)tick};ht_joint h=ht_drawing_dog_hand(&s);if((stage==3 && tick>=32) || stage==4 || (stage==5 && tick<=64)){unsigned q=ht_drawing_dog_open(&s);assert(h.x==297 && h.y==151+(256-(int)q)*70/256+62);}}
 for(unsigned native=0;native<2;++native){ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;unsigned n=native?HT_NATIVE_PIXELS:HT_PIXELS;for(unsigned stage=0;stage<=8;++stage){ht_drawing_state s={true,false,0,0,(uint8_t)stage,80};ht_drawing_study_render(&frozen,&s);memcpy(copy,ht_scene,n);ht_pack_mono(bits,120);ht_drawing_study_render(&frozen,&s);assert(!memcmp(copy,ht_scene,n) && !memcmp(&frozen,&ht,sizeof(ht)));}}
 free(bits);free(copy);free(memory);puts("Drawing comparison: actual route, physical pickup/return, reversible paper comparison, five-legged dog, frozen mirror/read guards, both controller interruption/retry/exit, bounded limbs and deterministic rasters PASS");
}
