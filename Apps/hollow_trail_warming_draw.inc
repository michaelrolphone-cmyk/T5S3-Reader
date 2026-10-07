/* The garden return joins the existing HEAT branch and runs back west. */
#define HT_WARMING_TOUCH_X 3120
#define HT_WARMING_TICKS 352
static unsigned ht_warming_amount(unsigned tick){return tick<96?0:ht_min(256,(tick-96)*256/256);}
static void ht_warming_pipe(const ht_game *g,unsigned warm){
 if(g->level!=7)return;
 int cx=g->camera/256,cy=g->camera_y/256,f=ht_level_land(7)[9].top-cy;
 int left=HT_PUZZLE_FIRST-65-cx,right=HT_WARMING_TOUCH_X-cx,heat=HT_PUZZLE_FIRST+HT_PUZZLE_SPACING-cx;
 if(right< -ht_view_margin() || left>HT_W+ht_view_margin())return;
 ht_limb(ht_scene,left,f-20,right,f-20,4,4,202);
 ht_limb(ht_scene,left,f-22,right,f-22,1,1,139);
 ht_limb(ht_scene,heat,f-29,heat,f-20,3,3,210);
 ht_limb(ht_scene,right,f-20,right,f+2,4,4,206);
 for(int n=0;n<5;++n){int x=left+24+n*57;ht_rect(ht_scene,x,f-25,3,11,226);ht_disk(ht_scene,x+1,f-24,1,124);}
 /* Condensation clears first beneath the palm, then along the return. */
 for(unsigned n=0;n<48;++n){
  int wx=HT_WARMING_TOUCH_X-(int)(n*6),x=wx-cx;
  if(warm && (HT_WARMING_TOUCH_X-wx)<=(int)warm*300/256)continue;
  int y=f-22+(int)(ht_hash(n+630)%5);
  ht_rect(ht_scene,x,y,1+(n%3==0),2,58+(int)(n%3)*13);
 }
 ht_checkpoint();
}
