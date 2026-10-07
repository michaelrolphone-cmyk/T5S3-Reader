/* VI–VII. The gallery and first room share the original support zero. All
 * masonry, glazing and planting below are scenery; no collision is added. */
#define HT_GALLERY_END 151
static int ht_gallery_soil(const ht_game *g,int world){return ht_surface_at(g,0,world);}
static void ht_gallery_wall(const ht_game *g){
 if(g->level!=6)return;
 int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
 if(HT_GALLERY_END-cx< -margin || -160-cx>HT_W+margin)return;
 /* A tapering back wall, behind the walking plane, opens onto the ferns.
  * Its last lower edge is buried in the exact ground drawn afterward. */
 for(int wx=ht_max(-160,cx-margin);wx<=ht_min(HT_GALLERY_END,cx+HT_W+margin);++wx){
  int f=ht_gallery_soil(g,wx)-cy,roof=f-101+(wx+160)*37/311;
  int mouth=ht_clamp(wx-89,0,62);
  int walltop=ht_min(f,roof+mouth*2);
  ht_rect(ht_scene,wx-cx,walltop,1,f-walltop+4,173);
  int ceiling=roof-mouth*5;
  ht_rect(ht_scene,wx-cx,-500-cy,1,ceiling+500+cy,207);
 }
 /* Uneven bright quarry opening, left behind rather than a route gate. */
 int foot=ht_gallery_soil(g,-29)-cy;
 static const int8_t opening[][2]={{0,0},{-3,-30},{1,-70},{11,-89},{31,-83},{40,-62},{36,-24},{39,0}};
 for(unsigned i=1;i<sizeof(opening)/sizeof(opening[0]);++i)
  ht_triangle(ht_scene,-29-cx+19,foot-37,-29-cx+opening[i-1][0],foot+opening[i-1][1],-29-cx+opening[i][0],foot+opening[i][1],45);
 ht_triangle(ht_scene,-29-cx+19,foot-37,-29-cx+opening[7][0],foot+opening[7][1],-29-cx+opening[0][0],foot+opening[0][1],45);
 ht_limb(ht_scene,-20-cx,foot-57,4-cx,foot-63,0,0,107);
 ht_limb(ht_scene,-26-cx,foot-26,1-cx,foot-34,0,0,106);
 /* Chisel scars catch that opening's last light, then dark damp seams. */
 for(int n=0;n<22;++n){
  int wx=-46+n*9,f=ht_gallery_soil(g,wx)-cy;
  int yy=f-26-(n*17%42);
  if(wx>95)continue;
  ht_limb(ht_scene,wx-cx,yy,wx+5-cx,yy-4,0,0,n<13?106:139);
  if(n%4==0)ht_limb(ht_scene,wx-cx,yy-12,wx+13-cx,yy-17,0,0,192);
 }
 ht_checkpoint();
}
static void ht_gallery_floor(const ht_game *g){
 if(g->level!=6)return;
 int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
 /* A material strip over the existing walkable surface. It never spans a gap. */
 for(int wx=ht_max(-43,cx-margin);wx<=ht_min(HT_GALLERY_END,cx+HT_W+margin);++wx){
  int f=ht_gallery_soil(g,wx)-cy;
  ht_rect(ht_scene,wx-cx,f,1,5,wx<66?139:157);
  int reflection=wx<10?61:86+(wx-10)/5;
  ht_rect(ht_scene,wx-cx,f+1+(wx/13&1),1,1,reflection);
  if(wx%19==0)ht_rect(ht_scene,wx-cx,f+4,3,1,193);
 }
 /* Damp stone gives way to rooted ferns at the same mouth seen from the cage. */
 for(int i=0;i<4;++i){int wx=132+i*12;
  ht_fern(ht_scene,wx-cx,ht_gallery_soil(g,wx)-cy+2,13+i%2*6,199,611u+i*23u);
 }
 ht_checkpoint();
}
static void ht_glasshouse_arrival(const ht_game *g){
 if(g->level!=6)return;
 int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
 if(378-cx< -margin || 151-cx>HT_W+margin)return;
 /* Limewash and the felt-wrapped ankle pipe hug the retained soil, leaving
  * the crate and its original climb silhouette fully exposed. */
 for(int wx=167;wx<342;wx+=7){
  int f=ht_gallery_soil(g,wx)-cy;
  ht_rect(ht_scene,wx-cx,f+6,7,9,145);
  ht_rect(ht_scene,wx-cx,f+14,7,4,180);
  if(wx%3)ht_rect(ht_scene,wx-cx,f+6,4,2,92);
  ht_limb(ht_scene,wx-cx,f-5,wx+7-cx,ht_gallery_soil(g,wx+7)-cy-5,1,1,170);
 }
 for(int n=0;n<3;++n){int wx=181+n*63,f=ht_gallery_soil(g,wx)-cy;
  ht_rect(ht_scene,wx-3-cx,f-8,7,6,195);
  for(int wrap=0;wrap<3;++wrap)ht_limb(ht_scene,wx-3+wrap*2-cx,f-8,wx-2+wrap*2-cx,f-3,0,0,111);
 }
 /* Thorned outer wall and cut ivy are slender growth, never solid scenery. */
 int f=ht_gallery_soil(g,160)-cy;
 ht_limb(ht_scene,157-cx,f,154-cx,f-26,1,0,207);
 ht_limb(ht_scene,154-cx,f-18,162-cx,f-38,1,0,205);
 for(int n=0;n<4;++n){int y=f-8-n*7;ht_limb(ht_scene,156-cx,y,150+(n%2)*15-cx,y-4,0,0,207);}
 int leg=368,base=ht_gallery_soil(g,leg)-cy;
 ht_limb(ht_scene,leg-cx,base,leg-cx,base-26,1,1,190);
 ht_limb(ht_scene,leg-10-cx,base-26,leg+17-cx,base-26,1,1,175);
 ht_limb(ht_scene,350-cx,ht_gallery_soil(g,350)-cy-1,leg-3-cx,base-3,0,0,184);
 ht_limb(ht_scene,leg-3-cx,base-3,leg+2-cx,base-14,0,0,186);
 ht_limb(ht_scene,leg+2-cx,base-18,leg-cx,base-31,0,0,139); /* severed gap */
 for(int n=0;n<3;++n)ht_detail_leaf(leg-cx,base-21-n*4,leg+7-n*6-cx,base-19-n*5,2,144);
 /* Clips remain where glass does not. One surviving pane catches the sky,
  * inverted seedlings and the passerby's sleeve while they move beneath it. */
 int x=187-cx,y=ht_bed_floor()-cy;
 ht_triangle(ht_scene,x,y-67,x+28,y-74,x+29,y-48,54);
 ht_triangle(ht_scene,x,y-67,x+29,y-48,x+1,y-43,54);
 ht_limb(ht_scene,x,y-67,x+1,y-43,0,0,157);
 ht_limb(ht_scene,x+28,y-74,x+29,y-48,0,0,157);
 ht_limb(ht_scene,x+1,y-43,x+29,y-48,0,0,154);
 for(int n=0;n<3;++n){int xx=x+5+n*8;
  ht_limb(ht_scene,xx,y-57,xx+1,y-48,0,0,116);
  ht_limb(ht_scene,xx,y-55,xx-3,y-52,0,0,116);
 }
 if(g->x/256>=175 && g->x/256<=253){int sleeve=x+ht_clamp((g->x/256-175)*22/78,2,23);
  ht_limb(ht_scene,sleeve,y-64,sleeve+4,y-58,2,1,128);
 }
 for(int n=0;n<5;++n){int xx=181+n*25-cx;
  ht_rect(ht_scene,xx,y-76,3,4,206);ht_rect(ht_scene,xx+1,y-75,1,2,78);
  if(n==3)ht_triangle(ht_scene,xx,y-72,xx+6,y-75,xx+2,y-65,63);
 }
 ht_checkpoint();
}
