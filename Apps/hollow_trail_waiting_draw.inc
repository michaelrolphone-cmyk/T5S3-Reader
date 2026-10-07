/* The waiting place on the real marsh bank. No walking stick is present. */
static int ht_waiting_swing(unsigned tick){
 static const int8_t wave[16]={0,3,6,8,9,8,6,3,0,-3,-6,-8,-9,-8,-6,-3};
 return tick>=240?0:wave[(tick/3)&15u]*(int)(240-tick)/240;
}
static int ht_waiting_pull(unsigned tick){
 if(tick<72)return 0;
 return tick<104?(int)(tick-72)*6/32:6;
}
static void ht_waiting_room(const ht_game *g,unsigned pull,int swing){
 if(g->level!=4)return;
 int cx=g->camera/256,cy=g->camera_y/256,x=HT_WAITING_X-cx,f=ht_waiting_floor()-cy;
 if(x+145< -ht_view_margin() || x-80>HT_W+ht_view_margin())return;
 for(int side=0;side<2;++side){
  int wx=HT_WAITING_X-66+side*104,yy=ht_surface_at(g,4,wx)-cy;
  ht_limb(ht_scene,wx-cx,f-67,wx-cx,yy,2,2,218);
 }
 ht_triangle(ht_scene,x-74,f-70,x+43,f-65,x+48,f-54,181);
 ht_triangle(ht_scene,x-74,f-70,x+48,f-54,x-68,f-58,174);
 for(int n=0;n<6;++n)ht_limb(ht_scene,x-62+n*18,f-68+n/2,x-55+n*18,f-57+n/2,1,0,146);
 /* Folded instruction is tucked into the seam, never a hovering marker. */
 ht_triangle(ht_scene,x+21,f-63,x+28,f-62,x+24,f-55,68);
 ht_limb(ht_scene,x-57,f-11,x,f-11,2,2,208);
 ht_limb(ht_scene,x-53,f-10,x-54,ht_surface_at(g,4,HT_WAITING_X-54)-cy,1,1,232);
 ht_limb(ht_scene,x-5,f-10,x-4,ht_surface_at(g,4,HT_WAITING_X-4)-cy,1,1,232);
 /* Three worn pairs; the small scuffs stop halfway down. */
 for(int pair=0;pair<3;++pair)for(int foot=0;foot<2;++foot){
  int at=x-45+pair*16+foot*4;
  ht_rect(ht_scene,at,f-10,2,pair==2?4:8,129);
 }
 /* Sackcloth over the low, tilted chair; one leg has sunk into the bank. */
 ht_limb(ht_scene,x+21,f-31,x+23,f+3,2,1,215);
 ht_limb(ht_scene,x+6,f-11,x+5,f,1,1,224);
 ht_limb(ht_scene,x+5,f-12,x+22,f-11,2,2,221);
 ht_limb(ht_scene,x+7,f-19,x+23,f-18,1,1,218);
 ht_triangle(ht_scene,x+6,f-14,x+20,f-14,x+19,f-9,157);
 for(int n=0;n<4;++n)ht_rect(ht_scene,x+7+n*3,f-13,1,4,197);
 ht_rect(ht_scene,x+21,f,5,4,166);ht_rect(ht_scene,x+20,f,7,1,205);
 /* Small platform stays on this bank. Smooth rounded groove, no stick. */
 ht_triangle(ht_scene,x+25,f,x+25,f-8,x+35,f-10,175);
 ht_triangle(ht_scene,x+25,f,x+35,f-10,x+35,f-2,175);
 ht_rect(ht_scene,x+29,f-9,2,8,99);ht_disk(ht_scene,x+30,f-2,1,99);
 /* Tied rope on their side; worn oval and patient whipping remain visible. */
 ht_disk(ht_scene,x+38,f-24,4,134);ht_disk(ht_scene,x+38,f-24,2,228);
 for(int n=0;n<4;++n)ht_rect(ht_scene,x+40,f-23+n*2,3,1,163);
 ht_limb(ht_scene,x+38,f-24,x+46,f-14,1,1,213);
 /* Five watermarks and the bell rope, including a lower child's loop. */
 ht_rect(ht_scene,x+41,f-43,5,39,183);
 for(int n=0;n<5;++n)ht_rect(ht_scene,x+41,f-40+n*6,5,1,85);
 int rope=x+48;
 ht_limb(ht_scene,rope,f-49,rope,f-15+(int)pull,1,1,217);
 ht_disk(ht_scene,rope,f-14+(int)pull,3,219);ht_disk(ht_scene,rope,f-14+(int)pull,1,116);
 ht_limb(ht_scene,rope,f-49,x+125,f-49,0,1,148);
 /* Bell at the opposite bank, using that bank's actual ground height. */
 int bx=x+125,by=ht_surface_at(g,5,HT_WAITING_X+125)-cy;
 ht_limb(ht_scene,bx-12,by,bx-12,by-54,1,1,173);
 ht_limb(ht_scene,bx-12,by-54,bx+12,by-54,1,1,178);
 int bell=bx+swing,bottom=by-36+ht_abs(swing)/3;
 ht_limb(ht_scene,bx,by-54,bell,bottom-10,1,1,191);
 ht_triangle(ht_scene,bell-4,bottom-10,bell-8,bottom,bell+8,bottom,211);
 ht_triangle(ht_scene,bell-4,bottom-10,bell+8,bottom,bell+4,bottom-10,211);
 ht_rect(ht_scene,bell-9,bottom,18,2,224);
 if(swing)ht_rect(ht_scene,bell+(swing>0?5:-8),bottom,3,1,80);
 ht_checkpoint();
}
