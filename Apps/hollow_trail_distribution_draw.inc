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

/* The existing feed lever validates 4/4/0. Merely filling those measures,
 * reading either list, or a failed release must not light the service line. */
static int ht_distribution_lamp_level(const ht_game *g,unsigned index){
 if(g->level!=2 || !g->puzzle.solved || g->puzzle.value[0]!=4 ||
    g->puzzle.value[1]!=4 || g->puzzle.value[2] || index>=4)return 0;
 return ht_clamp((int)g->puzzle.opening-(int)index*10,0,18)*256/18;
}
static void ht_distribution_service_line(const ht_game *g){
 if(g->level!=2)return;
 int cx=g->camera/256,cy=g->camera_y/256;
 for(unsigned n=0;n<4;++n){
  int wx=2840+(int)n*130,x=wx-cx,foot=ht_surface_at(g,9,wx)-cy;
  if(x< -42-ht_view_margin() || x>HT_W+42+ht_view_margin())continue;
  int light=ht_distribution_lamp_level(g,n),y=foot-86;
  /* Low tanks recede behind the player-operated measuring vessels. */
  ht_rect(ht_scene,x-38,foot-65,73,64,123);
  ht_limb(ht_scene,x-37,foot-65,x+34,foot-65,3,3,150);
  ht_limb(ht_scene,x-35,foot-60,x-35,foot-3,1,1,155);
  ht_rect(ht_scene,x-30,foot-28,59,1,142);
  ht_rect(ht_scene,x+25,foot-58,2,55,150-light*38/256);
  /* Fixtures sit behind the existing vessels; nothing is a new foothold. */
  ht_limb(ht_scene,x,y+1,x,foot,1,1,159);
  ht_limb(ht_scene,x,y+1,x+10,y+1,1,1,159);
  ht_rect(ht_scene,x+5,y+1,12,16,208);ht_rect(ht_scene,x+8,y+4,6,10,137-light*125/256);
  ht_triangle(ht_scene,x+3,y+1,x+11,y-5,x+19,y+1,194);
  if(light){ht_limb(ht_scene,x+20,y+4,x+23,y+4,0,0,137-light*105/256);ht_rect(ht_scene,x-3,foot-2,9,1,145-light*58/256);}
 }
}
