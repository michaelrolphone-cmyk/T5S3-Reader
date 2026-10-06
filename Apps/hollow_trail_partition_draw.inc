#define HT_PARTITION_TICKS 384u
static unsigned ht_partition_tick(const ht_game *g){return g->partition_view_active?g->partition_view_tick:g->puzzle.stage?HT_PARTITION_TICKS:0;}
static int ht_partition_leaf(unsigned tick,int row){
 int q=(int)tick-64-row*40;
 return q<=0 || q>=96?0:(q<48?q:96-q)*3/48;
}
static void ht_partition_pane(const ht_game *g,int top){
 int x=HT_GOAL-g->camera/256,height=96-g->puzzle.opening*2;
 unsigned tick=ht_partition_tick(g);int clear=ht_clamp((int)tick-160,0,192)*96/192;
 /* The original sliding boundary keeps the same aperture/opening geometry. */
 ht_rect(ht_scene,x-16,top-96,2,height,226);ht_rect(ht_scene,x+22,top-96,2,height,226);
 ht_rect(ht_scene,x-16,top-96+height-2,40,2,226);
 for(int col=0;col<6;++col){
  int front=clear==0 || clear==96?clear:ht_clamp(clear+(col*13)%17-8,0,96);
  for(int y=front;y<height-4;++y)ht_rect(ht_scene,x-14+col*6,top-94+y,6,1,104+((y+col*3)%11<3?19:0));
 }
 for(int n=0;n<24;++n){
  int dx=x-11+(n%6)*6,dy=top-87+(n/6)*21;
  int slide=ht_clamp((int)tick-144-(n%6)*8,0,240);
  dy+=slide*slide/480;
  int front=clear==0 || clear==96?clear:ht_clamp(clear+((n%6)*13)%17-8,0,96);
  if(dy>=top-96+height-3 || dy<top-93+front)continue;
  ht_rect(ht_scene,dx,dy,slide>24?2:1,slide>24?4:2,45);
  if(slide>24)ht_rect(ht_scene,dx+1,dy+2,1,3,191);
 }
 if(height>28)ht_limb(ht_scene,x-11,top-89,x+17,top-70,1,1,62);
 ht_checkpoint();
}
static void ht_partition_response(const ht_game *g,int top){
 if(g->level!=6)return;
 int x=HT_GOAL-g->camera/256;if(x< -100 || x>HT_W+160)return;
 unsigned tick=ht_partition_tick(g);int lift=ht_clamp((int)tick-32,0,32)*18/32;
 /* Upper shutter and catch are on the existing circular vent. */
 ht_rect(ht_scene,x-13,top-115,32,20-lift,182);
 for(int y=0;y<20-lift;y+=5)ht_rect(ht_scene,x-12,top-114+y,30,1,84);
 ht_rect(ht_scene,x+21,top-114-lift/6,6,3,237);
 ht_disk(ht_scene,x+25,top-111,3,ht_mirror_receiver(&g->puzzle)==1?20:215);
 ht_rect(ht_scene,x-22,top-29,6,10,225);
 if(tick<352)for(int i=0;i<3;++i)ht_rect(ht_scene,x-22+i*2,top-28+i*2,1,3,55);
 else ht_disk(ht_scene,x-19,top-25,2,ht_mirror_lit(&g->puzzle)?10:72);
 int first=HT_PUZZLE_FIRST-g->camera/256;
 if(ht_mirror_receiver(&g->puzzle)==1){
  ht_limb(ht_scene,first+120,top-104,x+25,top-111,1,1,23);
  if(g->partition_view_active && tick<32){
   int dropx=x-32,dropy=top-125+(int)tick;
   ht_rect(ht_scene,dropx,dropy,1,3,31);
   if(tick>=15 && tick<=19)ht_disk(ht_scene,dropx,dropy,3,0);
  }
 }else if(ht_mirror_lit(&g->puzzle))ht_limb(ht_scene,first+108,top-63,x-19,top-25,1,1,23);
 /* Cold air touches the nearest row, then the next, with bounded deflection. */
 for(int row=0;row<3;++row){
  int px=x-52-row*38,sway=ht_partition_leaf(tick,row);
  ht_rect(ht_scene,px-12,top-3,24,3,193);
  for(int stem=0;stem<3;++stem){
   int bx=px-8+stem*8,tip=bx-sway;
   ht_limb(ht_scene,bx,top-3,tip,top-19,1,1,221);
   ht_limb(ht_scene,tip,top-14,tip-7,top-18+sway,2,1,191);
   ht_limb(ht_scene,tip,top-11,tip+7,top-16+sway,2,1,205);
  }
 }
}
