/* Page 21 stays on the original support-one service pavement. The shed and
 * its conduit are scenery; no new platform or usable switch is introduced. */
static int ht_gauge_x(void){return ht_evidence_x(7,0);}
static int ht_gauge_floor(void){return ht_land_height(7,1,ht_gauge_x());}
static ht_joint ht_gauge_contact(void){return ht_joint_at(ht_gauge_x()+7,ht_gauge_floor()-24);}
static int ht_gauge_tremor(unsigned tick){return (int)((tick/3)%3)-1;}
static void ht_gauge_approach(const ht_game *g){
 if(g->level!=7)return;
 int cx=g->camera/256,cy=g->camera_y/256;
 if(450-cx< -ht_view_margin() || 114-cx>HT_W+ht_view_margin())return;
 /* Posts and puddle sit on existing support zero, before the wall reveal.
  * Only water and the light rail flex; the dam and camera stay massive. */
 int tremor=ht_gauge_tremor(g->ticks);
 for(int wx=120;wx<=440;wx+=40){
  int x=wx-cx,y=ht_surface_at(g,0,wx)-cy;
  ht_limb(ht_scene,x,y,x,y-23,1,1,203);
  if(wx<440)ht_limb(ht_scene,x,y-23,x+40,y-23+tremor,1,1,206);
  ht_disk(ht_scene,x+12,y-21+tremor,1,51);ht_limb(ht_scene,x+27,y-22,x+27,y-19,0,0,63);
 }
 int x=278-cx,y=ht_surface_at(g,0,278)-cy;
 ht_limb(ht_scene,x-22,y,x+22,y,2,2,91);
 for(int n=0;n<3;++n)ht_limb(ht_scene,x-17+n*7,y-1,x-6+n*9,y-1+tremor,0,0,38);
 ht_rect(ht_scene,x+29,y-1,15,3,198);
 for(int n=0;n<4;++n)ht_rect(ht_scene,x+31+n*3,y-1,1,3,73);
 ht_checkpoint();
}
static void ht_gauge_room(const ht_game *g){
 if(g->level!=7)return;
 int x=ht_gauge_x()-g->camera/256,f=ht_gauge_floor()-g->camera_y/256;
 if(x+173< -ht_view_margin() || x-24>HT_W+ht_view_margin())return;
 ht_rect(ht_scene,x-24,f-70,194,70,155);
 for(int n=0;n<4;++n)ht_limb(ht_scene,x-22,f-12-n*16,x+169,f-12-n*16,0,0,142);
 ht_limb(ht_scene,x-26,f-71,x+173,f-71,3,3,204);
 ht_rect(ht_scene,x+158,f-54,12,54,86);ht_rect(ht_scene,x+158,f-54,3,54,209);
 /* Broad cold pipe, bolted gauge and mica sheet with irregular retaining tabs. */
 ht_limb(ht_scene,x-16,f-14,x+153,f-14,5,5,192);
 ht_limb(ht_scene,x-15,f-16,x+154,f-16,1,1,122);
 ht_limb(ht_scene,x,f-16,x,f-26,2,2,216);
 ht_disk(ht_scene,x,f-28,12,226);ht_disk(ht_scene,x,f-28,9,72);
 ht_limb(ht_scene,x,f-28,x-5,f-33,0,0,226);ht_limb(ht_scene,x+2,f-28,x-3,f-33,0,0,146);
 ht_triangle(ht_scene,x+5,f-36,x+8,f-33,x+3,f-33,211);ht_disk(ht_scene,x+7,f-29,1,211);
 for(int n=0;n<3;++n)ht_rect(ht_scene,x-11+n*10,f-37+n%2*19,3,2,222);
 ht_limb(ht_scene,x,f-46,x,f-56,1,1,227);ht_disk(ht_scene,x,f-47,5,50);
 ht_triangle(ht_scene,x-9,f-48,x,f-56,x+9,f-48,226);
 /* The ordinary switch and actual divided conduit echo the wall drawing. */
 ht_rect(ht_scene,x+43,f-34,15,20,212);ht_limb(ht_scene,x+50,f-30,x+50,f-22,2,2,108);
 ht_limb(ht_scene,x+50,f-34,x+50,f-59,1,1,211);
 ht_limb(ht_scene,x,f-56,x,f-59,1,1,211);ht_limb(ht_scene,x,f-59,x+145,f-59,1,1,211);
 ht_limb(ht_scene,x+145,f-59,x+145,f-8,1,1,211);
 ht_rect(ht_scene,x+72,f-49,62,30,65);
 ht_limb(ht_scene,x+77,f-33,x+95,f-33,0,0,205);
 ht_limb(ht_scene,x+95,f-33,x+95,f-42,0,0,205);ht_limb(ht_scene,x+95,f-42,x+127,f-42,0,0,205);
 ht_limb(ht_scene,x+95,f-33,x+95,f-25,0,0,205);ht_limb(ht_scene,x+95,f-25,x+127,f-25,0,0,205);
 ht_disk(ht_scene,x+95,f-33,1,205);ht_checkpoint();
}
