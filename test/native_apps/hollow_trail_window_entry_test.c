#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "../../Apps/hollow_trail_cutscene.inc"
static void arrive(void) {
 memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
 ht.x=2040*256;ht.y=ht_signal_window_floor()*256;ht.grounded=true;
 for(int i=0;i<100;++i)ht_step_controls(1,0,false,false);
 assert(ht.x==(HT_SIGNAL_WINDOW_LEFT-5)*256 && !ht.traversal.window_open && ht_signal_window_near(&ht)==1);
}
static void contact(void) {
 ht_person_pose p=ht_person_pose_at(ht.x/256-ht.camera/256,ht.y/256-ht.camera_y/256,&ht);
 for(int i=0;i<2;++i) {
  int dx=p.hand[i].x-p.shoulder.x,dy=p.hand[i].y-p.shoulder.y;
  if(dx*dx+dy*dy>144)fprintf(stderr,"phase %d hand %d distance2 %d\n",ht.traversal.window_phase,i,dx*dx+dy*dy);
  if(dx*dx+dy*dy>144)fprintf(stderr,"body=%d,%d shoulder=%d,%d hand=%d,%d facing%d\n",ht.x/256,ht.y/256,p.shoulder.x+ht.camera/256,p.shoulder.y+ht.camera_y/256,p.hand[i].x+ht.camera/256,p.hand[i].y+ht.camera_y/256,ht.facing);
  assert(dx*dx+dy*dy<=144);
  dx=p.foot[i].x-p.hip.x;dy=p.foot[i].y-p.hip.y;
  if(dx*dx+dy*dy>225)fprintf(stderr,"phase %d foot %d distance2 %d\n",ht.traversal.window_phase,i,dx*dx+dy*dy);
  assert(dx*dx+dy*dy<=225);
  int wx=p.foot[i].x+ht.camera/256,wy=p.foot[i].y+ht.camera_y/256;
  assert(wy<=ht_signal_window_floor());
  if(wx>=HT_SIGNAL_WINDOW_LEFT && wx<=HT_SIGNAL_WINDOW_RIGHT) {
   if(wy>ht_signal_window_sill())fprintf(stderr,"phase %d foot %d crosses sill at %d/%d\n",ht.traversal.window_phase,i,wx,wy);
   assert(wy<=ht_signal_window_sill());
  }
 }
}
int main(void) {
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY);assert(memory);ht_bind(memory);ht_bind_native(memory);
 arrive();
 for(int i=0;i<150;++i){ht_step_controls(1,0,i%30==0,true);assert(ht.x<=(HT_SIGNAL_WINDOW_LEFT-5)*256);}
 arrive();assert(ht_traversal_interact() && ht.traversal.mode==HT_WINDOW);
 ht_game before=ht;ht_step_controls(1,0,false,false);assert(ht_cutscene_signal_arrival(&before,&ht));contact();
 for(int i=1;i<144;++i){int x=ht.x,y=ht.y;ht_step_controls(1,0,false,false);assert(ht_abs(ht.x-x)<256 && ht_abs(ht.y-y)<256);contact();}
 assert(ht.traversal.mode==HT_FREE && ht.x==(HT_SIGNAL_WINDOW_RIGHT+5)*256 && ht.grounded && ht.traversal.window_open==32);
 assert(!ht.evidence && !ht.scene_evidence && !ht.puzzle.solved);
 ht_step_controls(-1,0,false,false);assert(ht_signal_window_near(&ht)==-1 && ht_traversal_interact());
 for(int i=0;i<144;++i){ht_step_controls(-1,0,false,false);contact();}
 assert(ht.traversal.mode==HT_FREE && ht.x==(HT_SIGNAL_WINDOW_LEFT-5)*256 && ht.traversal.window_open==32);
 arrive();assert(ht_traversal_interact());for(int i=0;i<90;++i)ht_step_controls(1,0,false,false);
 int x=ht.x,y=ht.y,phase=ht.traversal.window_phase;
 for(int i=0;i<90;++i){ht_step_controls(0,0,i==3,i==3);assert(ht.x==x && ht.y==y && ht.traversal.window_phase==phase);}
 assert(ht_traversal_interact() && ht.traversal.mode==HT_WINDOW);
 ht_spawn(false);assert(ht.traversal.mode==HT_FREE && ht.traversal.window_open==32 && ht.traversal.window_print);
 ht_spawn(true);assert(!ht.traversal.window_open && !ht.traversal.window_print);
 /* A physical trace is revealed by a flash, and turns away with the leaf. */
 arrive();assert(ht_traversal_interact());ht.camera=1900*256;ht.camera_y=-240*256;
 uint8_t *copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*plain=malloc(HT_NATIVE_PIXELS/8);assert(copy&&bits&&plain);
 for(int native=0;native<2;++native) {
  ht_native_active=native!=0;ht_native_foreground_half_y=false;ht_world_scale=256;ht_scene=native?ht_native_a:ht_scene_low;
  int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;unsigned contrast[2]={0,0};
  for(int dark=0;dark<2;++dark) {
   ht.ticks=dark?70:8;ht.traversal.window_print=true;ht_game frozen=ht;
   memset(ht_scene,0,bytes);ht_signal_window_draw(&ht,false);memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,120);
   assert(!memcmp(&ht,&frozen,sizeof(ht)));
   ht.traversal.window_print=false;memset(ht_scene,0,bytes);ht_signal_window_draw(&ht,false);ht_pack_mono(plain,120);
   for(int i=0;i<bytes;++i)contrast[dark]+=(unsigned)ht_abs((int)copy[i]-ht_scene[i]);
   if(!dark)assert(memcmp(bits,plain,HT_NATIVE_PIXELS/8));
  }
  assert(contrast[0]>contrast[1] && contrast[1]>0);
  ht.traversal.window_open=32;ht.traversal.window_print=true;
  memset(ht_scene,0,bytes);ht_signal_window_draw(&ht,false);memcpy(copy,ht_scene,bytes);
  ht.traversal.window_print=false;memset(ht_scene,0,bytes);ht_signal_window_draw(&ht,false);assert(!memcmp(copy,ht_scene,bytes));
  ht.traversal.window_open=0;
 }
 free(plain);free(bits);free(copy);
 free(memory);puts("Window: physical closed stop, shared opening/sill, bounded crossing, hand/foot reach, pause/reverse and retry PASS");
}
