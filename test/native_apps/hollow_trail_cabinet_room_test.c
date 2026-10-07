#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned light(int x0,int y0,int x1,int y1){
 unsigned n=0;int scale=ht_native_active?2:1,w=ht_scene_width();
 for(int y=y0*scale;y<y1*scale;++y)for(int x=x0*scale;x<x1*scale;++x)n+=ht_scene[y*w+x]<70;
 return n;
}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY+32),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*base=malloc(HT_NATIVE_PIXELS/8);
 assert(memory&&copy&&bits&&base);memset(memory+HT_MEMORY+HT_NATIVE_MEMORY,0x5a,32);ht_bind(memory);ht_bind_native(memory);
 memset(&ht,0,sizeof(ht));ht.level=9;ht_select_level(9);ht_spawn(true);ht.puzzle.solved=true;ht.x=HT_PUZZLE_GATE*256;ht.y=ht_level_land(9)[9].top*256;ht.grounded=true;
 for(int native=0;native<2;++native){
  ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
  ht.camera=(HT_GOAL-240)*256;ht.camera_y=(ht_level_land(9)[9].top-190)*256;ht.intimacy=128;ht.vista=ht.drop_zoom=0;ht.sway_phase=ht.rotation_phase=0;
  for(unsigned choice=0;choice<=2;++choice){
   ht.verdict=choice;ht.verdict_read=false;ht.ticks=0;ht_game frozen=ht;ht_render_scene();int bytes=native?HT_NATIVE_PIXELS:HT_PIXELS;
   memcpy(copy,ht_scene,bytes);ht_render_scene();assert(!memcmp(copy,ht_scene,bytes)&&!memcmp(&ht,&frozen,sizeof(ht)));
   ht_native_active=native!=0;ht_native_foreground_half_y=false;ht_world_scale=256;ht_scene=native?ht_native_a:ht_scene_low;
   ht_game v=ht;v.camera=(HT_GOAL-240)*256;
   memset(ht_scene,180,bytes);ht_cabinet_case(&v,180);
   unsigned master=light(226,133,238,156),pen=light(243,158,254,161);
   if(choice==1){assert(master>100 && !pen);ht_pack_mono(base,120);}
   else if(choice==2){assert(!master && pen>16);ht_pack_mono(bits,120);assert(memcmp(base,bits,HT_NATIVE_PIXELS/8));}
   else assert(!master && pen>16);
   memcpy(copy,ht_scene,bytes);v.ticks=36;memset(ht_scene,180,bytes);ht_cabinet_case(&v,180);
   if(choice==2)assert(memcmp(copy,ht_scene,bytes));else assert(!memcmp(copy,ht_scene,bytes));
   assert(!ht.verdict_read && ht.verdict==choice);
  }
  ht_game g=ht;g.level=8;memset(ht_scene,180,ht_scene_width()*ht_scene_height());memcpy(copy,ht_scene,ht_scene_width()*ht_scene_height());ht_cabinet_room(&g);assert(!memcmp(copy,ht_scene,ht_scene_width()*ht_scene_height()));
  g.level=9;g.door_stage=HT_DOOR_NOTEBOOK;ht_cabinet_room(&g);assert(!memcmp(copy,ht_scene,ht_scene_width()*ht_scene_height()));
 }
 ht_game g=ht;g.camera=g.camera_y=0;g.x=HT_PUZZLE_GATE*256;g.y=ht_level_land(9)[9].top*256;g.vx=g.vy=0;g.facing=1;
 int f=ht_level_land(9)[9].top;ht_person_pose p=ht_person_pose_at(HT_PUZZLE_GATE,f,&g);
 for(unsigned choice=0;choice<=2;++choice)for(int grip=0;grip<2;++grip){int x=HT_GOAL+(grip?-24:-35),y=f-27+((choice==(unsigned)(grip+1))?8:0);int dx=x-p.shoulder.x,dy=y-p.shoulder.y;assert(dx*dx+dy*dy<=144);}
 for(int n=0;n<32;++n)assert(memory[HT_MEMORY+HT_NATIVE_MEMORY+n]==0x5a);
 free(base);free(bits);free(copy);free(memory);
 puts("Cabinet room: mutually exclusive visible releases, live-only pen movement, mono distinction, reachable grips, immutable choice/read state and ending exclusion PASS");
}
