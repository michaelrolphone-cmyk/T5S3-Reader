/* The original controls/contact regression remains runnable unchanged. */
#define main garden_night_baseline_main
#include "hollow_trail_garden_night_test.c"
#undef main
static void enter_memory(unsigned t){reset();action();input(0,8);action();input(0,8);ticks(160);assert(ht_night.stage==2);ticks(t);assert(ht_night.memory_tick==t);}
static void memory_clock(const ht_night_state *s){equal_clock(s);assert(s->memory_tick==ht_night.memory_tick);}
static void interruptions(void){
 const unsigned cues[]={128,215,320,478,552,648,711,820,896};
 for(unsigned n=0;n<sizeof(cues)/sizeof(cues[0]);++n){
  enter_memory(cues[n]);ht_game frozen=ht;ht_night_state before=ht_night;
  press(source?64:256);assert(ht_night.paused);ticks(40);memory_clock(&before);action();memory_clock(&before);
  press(source?128:512);assert(reading);ticks(30);memory_clock(&before);press(source?128:512);input(0,8);assert(!reading && ht_night.paused);press(source?64:256);input(0,8);
  before=ht_night;pad_failure=true;input(0,2);ticks(40);memory_clock(&before);pad_failure=false;input(source?2:1,2);ticks(20);memory_clock(&before);assert(ht_input_rearm);input(0,8);
  cancel();assert(!ht_night.active && ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));input(source?2:1,2);assert(!ht_night.active);input(0,8);action();input(0,8);assert(ht_night.active && ht_night.stage==0 && ht_night.memory_tick==0);
  action();input(0,8);ticks(160);ticks(cues[n]);assert(ht_night.stage==2);
  action();input(0,8);ticks(128);assert(ht_night.stage==4);action();input(0,8);ticks(160);assert(!ht_night.active && ht_input_rearm && !memcmp(&ht,&frozen,sizeof(ht)));
 }
 enter_memory(600);host_exit=true;input(0,8);assert(quitting);
}
static unsigned changed(const uint8_t *a,const uint8_t *b,unsigned n){unsigned count=0;for(unsigned i=0;i<n;++i)count+=a[i]!=b[i];return count;}
int main(void){
 assert(garden_night_baseline_main()==0);
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*gray=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*before=malloc(HT_NATIVE_PIXELS/8);assert(memory&&gray&&bits&&before);ht_bind(memory);ht_bind_native(memory);ht_reader_bitmap=bits;
 for(source=0;source<2;++source)interruptions();
 source=0;enter_memory(0);ht_game frozen=ht;
 for(unsigned t=0;t<1200;++t){ht_night_step(&ht_night);assert(ht_night.active && ht_night.stage==2 && ht_night.memory_tick==ht_min((int)t+1,(int)HT_SISTER_MEMORY_END));assert(!memcmp(&ht,&frozen,sizeof(ht)));}
 assert(!ht_sister_memory_active(&ht_night));assert(ht_night_action(&ht_night) && ht_night.stage==3);
 /* The knuckle ridge physically meets the lower cheek, and the held pot never
  * detaches from the hand. The arm's reach stays inside the ordinary sleeve. */
 ht_joint hand=ht_sister_folded_hand();assert(hand.x>=196 && hand.x<=209 && hand.y>=151 && hand.y<=158);
 unsigned last=256;for(unsigned t=0;t<192;++t){unsigned crease=ht_sister_crease(t);assert(crease<=last);last=crease;hand=ht_sister_tea_hand(t);int dx=hand.x-258,dy=hand.y-159;assert(dx*dx+dy*dy<=41*41);assert(hand.x>=288 && hand.x<=297 && hand.y>=140 && hand.y<=155);}assert(last==0);
 const unsigned cues[]={128,215,320,478,552,648,711,820,895,896};
 for(unsigned native=0;native<2;++native){ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;unsigned bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
  for(unsigned n=0;n<sizeof(cues)/sizeof(cues[0]);++n){ht_night_state state={true,false,2,0,80,(uint16_t)cues[n]};ht_night_study_render(&frozen,&state);memcpy(gray,ht_scene,bytes);ht_pack_mono(before,120);ht_night_study_render(&frozen,&state);ht_pack_mono(bits,120);assert(!memcmp(gray,ht_scene,bytes) && !memcmp(before,bits,HT_NATIVE_PIXELS/8));assert(!memcmp(&ht,&frozen,sizeof(ht)));}
  /* The crease is a real raster change, including in the production packing;
   * fixed tea pose here isolates its fading from the pot motion. */
  ht_world_scale=256;ht_rect(ht_scene,0,0,480,270,72);ht_sister_face(211,86,0,256,0,false,false);memcpy(gray,ht_scene,bytes);ht_pack_mono(before,120);
  ht_rect(ht_scene,0,0,480,270,72);ht_sister_face(211,86,0,0,0,false,false);ht_pack_mono(bits,120);assert(changed(gray,ht_scene,bytes)>8 && changed(before,bits,HT_NATIVE_PIXELS/8)>3);
 }
 free(before);free(bits);free(gray);free(memory);puts("Garden sister memory: earned once, bounded return, contact/crease pixels, unchanged live state, both rasters and HID/XInput pause/notes/fault/rearm/wake/cancel/replay/exit PASS");return 0;
}
