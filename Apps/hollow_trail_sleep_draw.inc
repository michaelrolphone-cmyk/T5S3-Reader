/* Chapter VII: raised sleeping places within the living glasshouse. */
#define HT_SLEEP_X 548
static int ht_sleep_floor(void){return ht_land_height(6,1,HT_SLEEP_X);}
static int ht_sleep_closure(unsigned stage,unsigned tick){
 if(stage==1)return tick<112?0:(int)ht_min(48,(tick-112)*48/64);
 if(stage==2)return 48;
 if(stage==3)return tick<64?48-(int)tick*48/64:0;
 return 0;
}
static int ht_sleep_leaf_ink(unsigned close){return 121+(int)close*110/64;}
/* Also drawn after the approaching body: the existing cloth is opaque. */
static void ht_sleep_screen(const ht_game *g){
 /* Uneven sacking seam, weighted corner, and the feet-height privacy gap. */
 int cx=g->camera/256,cy=g->camera_y/256;
 int curtain=HT_SLEEP_X+167-cx,f=ht_surface_at(g,1,HT_SLEEP_X+186)-cy;
 int left=ht_surface_at(g,1,HT_SLEEP_X+167)-cy,right=ht_surface_at(g,1,HT_SLEEP_X+210)-cy;
 ht_limb(ht_scene,curtain-3,left,curtain-3,f-40,1,1,205);ht_limb(ht_scene,curtain+47,right,curtain+47,f-40,1,1,205);
 ht_limb(ht_scene,curtain-3,f-40,curtain+47,f-40,1,1,190);
 ht_triangle(ht_scene,curtain,f-38,curtain+44,f-37,curtain+43,right-5,172);
 ht_triangle(ht_scene,curtain,f-38,curtain+43,right-5,curtain-1,left-5,162);
 for(int n=0;n<7;++n)ht_limb(ht_scene,curtain+19,f-36+n*4,curtain+23,f-34+n*4,1,0,216);
 ht_disk(ht_scene,curtain+43,right-5,3,226);
}
static void ht_sleep_room(const ht_game *g,unsigned close,bool wedge){
 if(g->level!=6)return;
 int cx=g->camera/256,cy=g->camera_y/256,x=HT_SLEEP_X-cx,f=ht_sleep_floor()-cy;
 if(x+260< -ht_view_margin() || x-30>HT_W+ht_view_margin())return;
 static const int8_t arch[][2]={{0,-66},{7,-76},{20,-84},{32,-87},{45,-81},{59,-68}};
 for(int n=0;n<5;++n){int xx=x-20+n*62;ht_limb(ht_scene,xx,f,xx,f-66,1,1,151);for(unsigned i=0;i+1<sizeof(arch)/sizeof(arch[0]);++i)ht_limb(ht_scene,xx+arch[i][0],f+arch[i][1],xx+arch[i+1][0],f+arch[i+1][1],1,1,146);}
 ht_limb(ht_scene,x+4,f-84,x+252,f-84,1,1,143);
 /* Seven straw pallets, with each frame's feet on the actual warm soil. */
 for(int n=0;n<7;++n){
  int wx=HT_SLEEP_X+38+n*29,xx=wx-cx,yy=ht_surface_at(g,1,wx)-cy,w=n==6?32:23;
  ht_limb(ht_scene,xx,yy-7,xx+w,yy-7,1,1,203);
  ht_limb(ht_scene,xx+2,yy-7,xx+2,ht_surface_at(g,1,wx+2)-cy,1,1,220);
  ht_limb(ht_scene,xx+w-3,yy-7,xx+w-3,ht_surface_at(g,1,wx+w-3)-cy,1,1,220);
  ht_triangle(ht_scene,xx,yy-10,xx+w-2,yy-12,xx+w+1,yy-7,83);
  ht_triangle(ht_scene,xx,yy-10,xx+w+1,yy-7,xx,yy-7,100);
  for(int straw=0;straw<(n==6?9:6);++straw)ht_limb(ht_scene,xx+2+straw*3,yy-7,xx+5+straw*3,yy-6,1,0,160);
 }
 ht_sleep_screen(g);
 /* Inside lock, missing key and the worn arc of repeated closing. */
 ht_rect(ht_scene,x-3,f-43,4,43,210);ht_rect(ht_scene,x-4,f-44,32,3,201);ht_rect(ht_scene,x+27,f-42,3,42,198);
 ht_limb(ht_scene,x+1,f+2,x+34,f+3,1,1,127);ht_limb(ht_scene,x+34,f+3,x+46,f+8,1,0,133);
 int edge=x+2+(int)close*25/64;
 ht_triangle(ht_scene,x,f-41,edge,f-39,edge,f-2,203);ht_triangle(ht_scene,x,f-41,edge,f-2,x,f,214);
 ht_rect(ht_scene,edge-3,f-25,5,9,241);ht_rect(ht_scene,edge-1,f-21,4,2,110);
 int width=ht_max(1,27-(int)close*25/64);
 ht_triangle(ht_scene,edge,f+1,x+28,f+1,x+28+width*2,f+10,54+(int)close/3);
 ht_triangle(ht_scene,edge,f+1,x+28+width*2,f+10,edge+width,f+8,66+(int)close/3);
 if(wedge)ht_triangle(ht_scene,x+3,f,x+10,f,x+9,f-3,179);
 for(int n=0;n<5;++n){
  int xx=x+39+n*7,yy=f-10-(n%3)*3;ht_limb(ht_scene,xx,f,xx-2,yy,1,1,215);
  ht_triangle(ht_scene,xx-2,yy+1,xx-10,yy-6,xx-3,yy-7,ht_sleep_leaf_ink(close));
  ht_triangle(ht_scene,xx-1,yy+3,xx+7,yy-3,xx+2,yy-3,ht_sleep_leaf_ink(close));
 }
 ht_checkpoint();
}
