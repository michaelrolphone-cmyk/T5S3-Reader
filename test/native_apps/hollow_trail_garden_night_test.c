/* First living bed reached from the actual glasshouse spawn. */
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
 memset(&ht,0,sizeof(ht));ht.level=6;ht_select_level(6);ht_spawn(true);ht_night=(ht_night_state){0};ht_bed=(ht_bed_state){0};ht_counts=(ht_counts_state){0};ht_drawing=(ht_drawing_state){0};ht_sleep=(ht_sleep_state){0};ht_partition=(ht_partition_state){0};
 ht_cutscene.active=false;ht_cutscene.finished=true;ht_cutscene_seen=63;reading=paused=quitting=loading=debug_jump=debug_select=jump_down=pause_down=false;held=previous=mapped=0;ht_pad_owned=ht_input_rearm=ht_pad_fault=false;ht_pad_source=-1;ht_pad_device=0;simulation_started=false;simulation_accumulator=0;
 report=(risc_usb_gamepad_state_v1){.connected=1,.device=1,.hat=8};pad=source?NULL:&gamepad;hid_pad=source?&gamepad:NULL;host_exit=pad_failure=false;app=&fake_app;
 walk_level=999;for(unsigned i=0;i<20000 && !ht_night_near(&ht);++i)walk_route_tick();assert(ht.level==6 && ht_night_near(&ht) && !ht.deaths);ht_input(1);input(0,8);
}
static void equal_clock(const ht_night_state *s){assert(s->stage==ht_night.stage && s->tick==ht_night.tick && s->ambient==ht_night.ambient && s->memory_tick==ht_night.memory_tick);}
static void controls(void){
 reset();ht_game frozen=ht;action();assert(ht_night.active && !reading);input(0,8);assert(!memcmp(&ht,&frozen,sizeof(ht)));
 for(unsigned stage=0;stage<6;++stage){
  ht_night.stage=(uint8_t)stage;ht_night.tick=40;ht_night.ambient=81;
  press(source?64:256);assert(ht_night.paused);ht_night_state before=ht_night;ticks(50);equal_clock(&before);action();equal_clock(&before);
  press(source?128:512);assert(reading);ticks(30);equal_clock(&before);press(source?128:512);input(0,8);assert(!reading && ht_night.paused);press(source?64:256);input(0,8);
  before=ht_night;pad_failure=true;input(0,2);ticks(30);equal_clock(&before);pad_failure=false;input(source?2:1,2);ticks(8);equal_clock(&before);assert(ht_input_rearm);input(0,8);
  cancel();assert(!ht_night.active && ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));input(source?2:1,2);assert(!ht_night.active);input(0,8);action();input(0,8);assert(ht_night.active && !ht_night.stage);
 }
 action();input(0,8);ticks(160);assert(ht_night.stage==2);unsigned ambient=ht_night.ambient;ticks(160);assert(ht_night.stage==2 && ht_night.ambient!=ambient && !memcmp(&ht,&frozen,sizeof(ht)));
 action();input(0,8);ticks(128);assert(ht_night.stage==4);unsigned rev=scene_revision;ticks(60);assert(scene_revision==rev);action();input(0,8);ticks(160);assert(!ht_night.active && ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
 input(0,8);action();assert(ht_night.active);host_exit=true;input(0,8);assert(quitting);
}
static void reach(const ht_person_pose *p,unsigned stage,unsigned tick){for(int i=0;i<2;++i){int dx=p->hand[i].x-p->shoulder.x,dy=p->hand[i].y-p->shoulder.y;if(dx*dx+dy*dy>144)fprintf(stderr,"night arm %u/%u %d/%d\n",stage,tick,dx,dy);assert(dx*dx+dy*dy<=144);dx=p->foot[i].x-p->hip.x;dy=p->foot[i].y-1-p->hip.y;if(dx*dx+dy*dy>225)fprintf(stderr,"night leg %u/%u %d/%d\n",stage,tick,dx,dy);assert(dx*dx+dy*dy<=225);}}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*mono=malloc(HT_NATIVE_PIXELS/8);assert(memory&&copy&&bits&&mono);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 for(source=0;source<2;++source)controls();
 source=0;reset();ht_game frozen=ht;
 for(unsigned stage=0;stage<6;++stage)for(unsigned tick=0;tick<=160;++tick){ht_night_state s={true,false,(uint8_t)stage,(uint16_t)tick,0,0};ht_person_pose p=ht_night_pose(&frozen,&s);reach(&p,stage,tick);}
 for(unsigned native=0;native<2;++native){ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;unsigned bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;for(unsigned stage=0;stage<6;++stage){ht_night_state s={true,false,(uint8_t)stage,100,120,0};ht_night_study_render(&frozen,&s);memcpy(copy,ht_scene,bytes);ht_pack_mono(mono,120);ht_night_study_render(&frozen,&s);ht_pack_mono(bits,120);assert(!memcmp(copy,ht_scene,bytes)&&!memcmp(mono,bits,HT_NATIVE_PIXELS/8)&&!memcmp(&ht,&frozen,sizeof(ht)));}
  /* Actual rendered head must share the world's caption-safe camera. This
   * catches a separate overlay camera leaving the resting body below its bed. */
  ht_game view=frozen;view.camera=(HT_NIGHT_X+14-240)*256;view.camera_y=(ht_night_floor()-164)*256;view.intimacy=512;view.vista=view.drop_zoom=0;view.rotation_phase=view.sway_phase=0;ht_frame_camera(&view,HT_CAMERA_BASELINE);
  ht_night_state standing={true,false,0,0,0,0};ht_person_pose pose=ht_night_pose(&view,&standing);int scale=ht_scene_scale(&view),hx=240+(pose.shoulder.x-240)*scale/256,hy=135+(pose.shoulder.y-5-135)*scale/256;
  ht_night_study_render(&frozen,&standing);int raster=native?2:1;assert(ht_scene[(hy*raster)*(native?HT_NATIVE_W:HT_W)+hx*raster]==255);
  ht_night_state resting={true,false,2,0,0,0};pose=ht_night_pose(&view,&resting);for(int side=0;side<2;++side){assert(pose.foot[side].x+view.camera/256>=HT_NIGHT_X+24 && pose.foot[side].x+view.camera/256<=HT_NIGHT_X+32);assert(pose.foot[side].y+view.camera_y/256==ht_night_floor()-9);}
  ht_night_state night={true,false,2,0,64,0};ht_night_study_render(&frozen,&night);ht_pack_mono(mono,120);night.ambient=128;ht_night_study_render(&frozen,&night);ht_pack_mono(bits,120);unsigned change=0;for(unsigned n=0;n<120*440;++n){unsigned delta=mono[n]^bits[n];for(unsigned bit=0;bit<8;++bit)change+=(delta>>bit)&1u;}fprintf(stderr,"night native=%u changed_pixels=%u\n",native,change);assert(change>8);
 }
 assert(ht_night_leaf_width(64)==0 && ht_night_leaf_width(128)==5);
 free(mono);free(bits);free(copy);free(memory);puts("Garden night: actual far pallet, chosen rest/morning, bounded limbs and leaf/pipe motion, both rasters, frozen crate/mirror/evidence and HID/XInput interruption/retry/exit PASS");return 0;
}
