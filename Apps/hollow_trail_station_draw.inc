/* Chapter IV station between the intact train and existing trestle. */
static void ht_station_room(const ht_game *g) {
 if(g->level!=3)return;
 int cx=g->camera/256,cy=g->camera_y/256,x=HT_STATION_TICKET_X-cx,f=ht_station_floor()-cy;
 if(x+56< -ht_view_margin() || x-57>HT_W+ht_view_margin())return;
 ht_rect(ht_scene,x-56,f-78,110,78,149);
 ht_triangle(ht_scene,x-61,f-78,x-5,f-96,x+60,f-78,218);
 ht_limb(ht_scene,x-61,f-78,x+60,f-78,2,2,232);
 ht_rect(ht_scene,x-56,f-76,5,76,212);
 ht_rect(ht_scene,x+51,f-76,4,76,221);
 for(int n=0;n<7;++n)ht_limb(ht_scene,x-49,f-9-n*10,x+49,f-9-n*10,0,0,158);
 ht_rect(ht_scene,x-48,f,97,4,186);
 for(int n=0;n<5;++n)ht_limb(ht_scene,x-46+n*22,f,x-43+n*22,f+3,0,0,128);
 /* Track-facing benches remain empty, with a little space at the end. */
 for(int n=0;n<2;++n) {
  int a=x-47+n*48;
  ht_limb(ht_scene,a,f-11,a+36,f-11,1,1,226);
  ht_limb(ht_scene,a,f-23,a+36,f-23,1,1,215);
  ht_limb(ht_scene,a+3,f-23,a+3,f,1,1,229);
  ht_limb(ht_scene,a+33,f-23,a+33,f,1,1,229);
  ht_limb(ht_scene,a+2,f-18,a+34,f-18,0,0,180);
 }
 /* Noticeboard, HOME ticket, cancellation book below, weather warning above. */
 ht_rect(ht_scene,x-13,f-61,42,29,207);
 for(int n=0;n<4;++n)ht_limb(ht_scene,x-10,f-56+n*6,x+26,f-56+n*6,0,0,151);
 ht_rect(ht_scene,x-6,f-49,23,10,62);
 ht_disk(ht_scene,x+12,f-44,1,207);
 ht_limb(ht_scene,x+11,f-44,x+13,f-44,0,0,151); /* punched hole exposes wood */
 ht_limb(ht_scene,x-5,f-44,x+14,f-44,0,0,137); /* persistent fold */
 ht_disk(ht_scene,x+4,f-48,1,229);
 ht_rect(ht_scene,x+3,f-72,16,10,80);
 ht_limb(ht_scene,x+5,f-69,x+16,f-69,0,0,157);
 ht_limb(ht_scene,x+5,f-66,x+15,f-66,0,0,159);
 ht_limb(ht_scene,x-10,f-29,x+28,f-29,1,1,225);
 ht_limb(ht_scene,x-7,f-29,x-7,f,1,1,210);
 ht_limb(ht_scene,x+24,f-29,x+24,f,1,1,210);
 ht_triangle(ht_scene,x-7,f-31,x+6,f-33,x+7,f-29,68);
 ht_triangle(ht_scene,x+7,f-29,x+8,f-33,x+24,f-31,76);
 ht_limb(ht_scene,x+7,f-33,x+7,f-29,0,0,174);
 ht_checkpoint();
}
static void ht_station_cutting(const ht_game *g) {
 if(g->level!=3)return;
 /* The abandoned parallel track ends east of the locomotive's forward end.
  * It is a background cutting; the existing foreground path stays passable. */
 int x=505-g->camera/256,y=208-g->camera_y/256;
 if(x+145< -ht_view_margin() || x-130>HT_W+ht_view_margin())return;
 ht_triangle(ht_scene,x-135,y,x-58,y-135,x+78,y,133);
 ht_triangle(ht_scene,x-58,y-135,x+40,y-120,x+111,y,114);
 ht_triangle(ht_scene,x-48,y-112,x+18,y-112,x+83,y-18,61); /* pale fresh scar */
 for(int n=0;n<5;++n)ht_limb(ht_scene,x-51+n*12,y-102+n*12,x+22+n*13,y-76+n*10,0,0,105);
 /* Rails enter the debris from the locomotive side and disappear beneath it. */
 ht_limb(ht_scene,x+28,y+2,x+143,y+2,1,1,188);
 ht_limb(ht_scene,x+29,y+7,x+143,y+7,1,1,191);
 for(int n=0;n<7;++n)ht_limb(ht_scene,x+39+n*15,y,x+35+n*15,y+10,1,1,165);
 for(int n=0;n<7;++n) {
  int a=x-75+n*23,h=18+(n*17)%37;
  ht_triangle(ht_scene,a,y,a+9,y-h,a+30,y,169+n%3*12);
  ht_limb(ht_scene,a+9,y-h,a+21,y-5,0,0,132);
 }
 /* Fallen trees, outward roots and telegraph wire follow the actual passage. */
 ht_limb(ht_scene,x-43,y-31,x+28,y-73,3,2,192);
 for(int n=0;n<4;++n)ht_limb(ht_scene,x-40,y-32,x-62+n*9,y-43+n*8,1,0,190);
 ht_limb(ht_scene,x+13,y-64,x+11,y-89,1,0,180);
 ht_limb(ht_scene,x+25,y-71,x+47,y-81,1,0,180);
 ht_limb(ht_scene,x+95,y-119,x+95,y-33,1,1,177);
 ht_limb(ht_scene,x+95,y-112,x+49,y-42,0,0,174);
 ht_checkpoint();
}
