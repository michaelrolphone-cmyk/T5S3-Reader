/* Chapter III's tied empty cans, on the existing distribution evidence terrace. */
static int ht_distribution_can_offset(unsigned tick) {
 static const int8_t swing[8]={0,1,1,0,0,-1,-1,0};
 return tick>=48?0:swing[tick&7u]*(int)(48-tick)/12;
}
static void ht_distribution_room(const ht_game *g,unsigned tick) {
 if(g->level!=2)return;
 int cx=g->camera/256,cy=g->camera_y/256,x=HT_DISTRIBUTION_X-53-cx,f=ht_distribution_floor()-cy;
 if(x+98< -ht_view_margin() || x-56>HT_W+ht_view_margin())return;
 for(int side=0;side<2;++side){
  int wx=HT_DISTRIBUTION_X-102+side*134,xx=wx-cx,foot=ht_surface_at(g,7,wx)-cy;
  ht_limb(ht_scene,xx,f-69,xx,foot,2,2,214);
 }
 ht_triangle(ht_scene,x-56,f-73,x+91,f-76,x+96,f-62,182);
 ht_triangle(ht_scene,x-56,f-73,x+96,f-62,x-49,f-61,170);
 for(int n=0;n<8;++n)ht_limb(ht_scene,x-48+n*19,f-72,x-41+n*19,f-62,1,0,155);
 /* Low wall follows the unchanged bank, with one worn seating edge. */
 for(int n=0;n<8;++n){
  int wx=HT_DISTRIBUTION_X-96+n*17,yy=ht_surface_at(g,7,wx)-cy;
  ht_rect(ht_scene,wx-cx,yy-12,16,12,169);
  ht_rect(ht_scene,wx-cx+1,yy-13,14,2,203);
 }
 int shift=ht_distribution_can_offset(tick);
 for(int n=0;n<4;++n){
  int wx=HT_DISTRIBUTION_X-87+n*29,xx=wx-cx+shift,yy=ht_surface_at(g,7,wx)-cy;
  ht_rect(ht_scene,xx-7,yy-24,15,23,196);
  ht_triangle(ht_scene,xx-7,yy-24,xx-3,yy-28,xx+7,yy-24,218);
  ht_rect(ht_scene,xx-4,yy-31,8,6,229);ht_rect(ht_scene,xx-2,yy-30,4,4,105);
  ht_limb(ht_scene,xx-5,yy-21,xx-4,yy-4,1,0,147);
  ht_limb(ht_scene,xx+5,yy-20,xx+5,yy-3,1,1,220);
  ht_rect(ht_scene,xx-5,yy-9,10,1,172);
  if(n<3){
   int next=ht_surface_at(g,7,wx+29)-cy;
   ht_limb(ht_scene,xx+1,yy-29,xx+15,(yy+next)/2-27,1,0,134);
   ht_limb(ht_scene,xx+15,(yy+next)/2-27,xx+30,next-29,0,1,134);
  } else {
   /* The last can contains two nested paper rolls. */
   ht_rect(ht_scene,xx-3,yy-32,4,10,73);
   ht_rect(ht_scene,xx+1,yy-34,3,12,101);
   ht_disk(ht_scene,xx-1,yy-32,2,171);
   ht_disk(ht_scene,xx+2,yy-34,1,180);
  }
 }
 ht_checkpoint();
}
