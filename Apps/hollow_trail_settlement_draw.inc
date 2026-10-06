/* Novella X: ordinary traces on the existing three street supports. These
 * walls and recessed yards are scenery behind the walkable profile, not new
 * platforms, entrances, evidence, occupants or controls. All roots sample the
 * same world surfaces as the route; camera/player position never spawns art. */
#define HT_SETTLEMENT_BOWL_X 590
#define HT_SETTLEMENT_SCRAPER_X 703
#define HT_SETTLEMENT_CISTERN_X 969
static bool ht_settlement_visible(const ht_game *g,int left,int right){
 int cx=g->camera/256,margin=ht_view_margin();
 return right-cx>=-margin && left-cx<=HT_W+margin;
}
static void ht_settlement_wall(const ht_game *g,int support,int left,int right,int height,int tone){
 int cx=g->camera/256,cy=g->camera_y/256,f=ht_surface_at(g,support,left)-cy;
 ht_rect(ht_scene,left-cx,f-height,right-left,height,tone);
 /* Whitewash survives in rough joints and below the cap; shoulder height is
  * darker. Fixed eight-pixel/stone work stays within these short footprints. */
 for(int row=0;row<height/10;++row){
  int y=f-height+4+row*10;
  for(int x=left+3+(row%2)*7;x<right-5;x+=19){
   int end=ht_min(x+15,right-2);
   ht_limb(ht_scene,x-cx,y,end-cx,y-1,0,0,row<2?tone-48:tone-16);
   ht_limb(ht_scene,end-cx,y,end-cx,y+7,0,0,tone-20);
  }
 }
 ht_limb(ht_scene,left-cx,f-height,right-cx,f-height,2,2,tone-37);
 ht_checkpoint();
}
static void ht_settlement_streets(const ht_game *g){
 if(g->level!=9 || g->door_stage)return;
 int cx=g->camera/256,cy=g->camera_y/256;
 if(ht_settlement_visible(g,347,422)){
  int f=ht_surface_at(g,0,380)-cy;
  ht_settlement_wall(g,0,347,420,37,162);
  /* A dark recess turns behind a rough corner. Its steps recede into the
   * wall; they do not bridge the real x430–454 gap. */
  ht_disk(ht_scene,380-cx,f-24,12,224);
  ht_rect(ht_scene,368-cx,f-24,24,24,224);
  for(int n=0;n<4;++n)ht_limb(ht_scene,369+n*2-cx,f-2-n*5,390-n*2-cx,f-2-n*5,0,0,161);
  ht_rect(ht_scene,391-cx,f-38,12,38,147);
  ht_limb(ht_scene,392-cx,f-37,392-cx,f,1,1,103);
  /* An adjacent roof sits near the next terrace's door height. */
  ht_limb(ht_scene,348-cx,f-40,414-cx,f-40,2,2,218);
  ht_limb(ht_scene,351-cx,f-42,412-cx,f-42,0,0,104);
  ht_checkpoint();
 }
 if(ht_settlement_visible(g,466,745)){
  int f=ht_surface_at(g,1,HT_SETTLEMENT_BOWL_X)-cy;
  ht_settlement_wall(g,1,468,494,88,163);
  ht_settlement_wall(g,1,552,624,68,141);
  ht_settlement_wall(g,1,645,741,61,156);
  ht_triangle(ht_scene,546-cx,f-69,582-cx,f-84,630-cx,f-69,210);
  ht_triangle(ht_scene,639-cx,f-62,689-cx,f-74,748-cx,f-62,203);
  ht_limb(ht_scene,546-cx,f-69,630-cx,f-69,2,2,228);
  ht_limb(ht_scene,639-cx,f-62,748-cx,f-62,2,2,229);
  /* Alley steps disappear behind the nearer wall corners. */
  ht_rect(ht_scene,623-cx,f-53,24,53,229);
  for(int n=0;n<5;++n)ht_limb(ht_scene,625+n-cx,f-2-n*6,646-n-cx,f-2-n*6,0,0,167);
  ht_rect(ht_scene,621-cx,f-68,7,68,177);
  ht_rect(ht_scene,642-cx,f-61,7,61,125);
  ht_limb(ht_scene,643-cx,f-59,643-cx,f-1,1,1,94);
  /* Closed doors: the clean threshold and leaf drift imply differing care,
   * without turning light or recent mud into proof of occupation. */
  ht_rect(ht_scene,565-cx,f-44,29,44,214);
  ht_rect(ht_scene,568-cx,f-41,23,41,185);
  ht_limb(ht_scene,570-cx,f-40,570-cx,f-3,0,0,121);
  ht_disk(ht_scene,587-cx,f-22,1,56);
  ht_limb(ht_scene,562-cx,f-1,596-cx,f-1,1,1,85);
  ht_rect(ht_scene,668-cx,f-42,28,42,219);
  ht_rect(ht_scene,671-cx,f-39,22,39,185);
  ht_disk(ht_scene,689-cx,f-20,1,64);
  for(int n=0;n<12;++n){int x=669+n*2,y=f-1-n%3;
   ht_limb(ht_scene,x-cx,y,x+3-cx,y-2,1,0,115+n%3*21);
  }
  /* Lit, clouded panes retain unidentifiable reflections only. */
  for(int n=0;n<2;++n){int x=(n?716:607)-cx,y=f-(n?44:50);
   ht_rect(ht_scene,x-8,y-1,17,26,222);ht_rect(ht_scene,x-5,y+2,11,19,77);
   ht_rect(ht_scene,x-5,y+11,11,10,115);
   for(int drop=0;drop<6;++drop)ht_rect(ht_scene,x-4+drop%3*3,y+11+drop/3*4,2,3,144);
   ht_rect(ht_scene,x+1,y+12,2,8,72);ht_limb(ht_scene,x-8,y+24,x+9,y+24,1,1,198);
  }
  /* A shallow inverted bowl rests on the swept sill, with a stone on its
   * upturned foot. The rim stays on the actual support-one floor. */
  int b=HT_SETTLEMENT_BOWL_X-cx;
  for(int row=0;row<6;++row){int half=3+row;
   ht_rect(ht_scene,b-half,f-6+row,half*2+1,1,71+row*10);
  }
  ht_limb(ht_scene,b-9,f-1,b+9,f-1,1,1,219);
  ht_limb(ht_scene,b-3,f-7,b+3,f-7,1,1,198);
  ht_triangle(ht_scene,b-3,f-8,b-1,f-12,b+4,f-8,159);
  ht_limb(ht_scene,b-1,f-12,b+3,f-10,0,0,87);
  /* Cleared iron blade and its separate, still-shaped mud heap. */
  int s=HT_SETTLEMENT_SCRAPER_X-cx;
  ht_limb(ht_scene,s-6,f-1,s-6,f-8,1,1,227);
  ht_limb(ht_scene,s+6,f-1,s+6,f-8,1,1,227);
  ht_limb(ht_scene,s-7,f-7,s+7,f-7,1,1,220);
  ht_limb(ht_scene,s-5,f-8,s+5,f-8,0,0,66);
  ht_triangle(ht_scene,s+11,f-1,s+16,f-6,s+24,f-1,194);
  ht_limb(ht_scene,s+13,f-3,s+16,f-6,0,0,127);
  /* Empty cord, seven separate clothespins, fixed shallow sag. Its endpoints
   * meet the two walls above their doors; nothing sways like a person. */
  int px=557,py=f-61;
  for(int n=1;n<=8;++n){int x=557+n*22,y=f-61+n+6*n*(8-n)/16;
   ht_limb(ht_scene,px-cx,py,x-cx,y,0,0,230);
   if(n<8){ht_limb(ht_scene,x-1-cx,y-2,x-1-cx,y+5,1,1,189);
    ht_limb(ht_scene,x+1-cx,y-2,x+1-cx,y+4,0,0,81);}
   px=x;py=y;
  }
  ht_disk(ht_scene,557-cx,f-61,2,222);ht_disk(ht_scene,733-cx,f-53,2,222);
  ht_checkpoint();
 }
 if(ht_settlement_visible(g,782,1005)){
  int f=ht_surface_at(g,2,HT_SETTLEMENT_CISTERN_X)-cy;
  ht_settlement_wall(g,2,784,1005,41,153);
  /* Two tiny beds occupy the back ledge; the front walking surface remains
   * continuous and at y180. The dead vine leans on its unequal trellis. */
  for(int bed=0;bed<2;++bed){int x=bed?865:798,w=bed?56:54;
   ht_rect(ht_scene,x-cx,f-15,w,11,190);ht_limb(ht_scene,x-cx,f-4,x+w-cx,f-4,2,2,138);
   if(bed)for(int n=0;n<6;++n){int at=x+5+n*8;
    ht_limb(ht_scene,at-cx,f-13,at+5-cx,f-9,1,0,231);
    ht_limb(ht_scene,at+2-cx,f-13,at+6-cx,f-12,0,0,111);}
  }
  ht_limb(ht_scene,807-cx,f-15,804-cx,f-58,1,1,187);
  ht_limb(ht_scene,840-cx,f-15,846-cx,f-55,1,1,195);
  for(int n=0;n<3;++n)ht_limb(ht_scene,804-cx,f-52+n*13,846-cx,f-49+n*13,0,0,174);
  ht_limb(ht_scene,824-cx,f-14,821-cx,f-34,1,1,218);
  ht_limb(ht_scene,821-cx,f-34,834-cx,f-47,1,0,218);
  ht_limb(ht_scene,834-cx,f-47,835-cx,f-60,0,0,218);
  ht_limb(ht_scene,821-cx,f-34,812-cx,f-44,0,0,218);
  /* A stone cistern and roof-water channel disappear under cracked covers,
   * without painting a false step over the actual gap at x760–770. */
  int x=HT_SETTLEMENT_CISTERN_X-cx;
  ht_rect(ht_scene,x-18,f-29,37,29,123);
  ht_limb(ht_scene,x-20,f-29,x+20,f-29,3,3,199);
  ht_limb(ht_scene,x-15,f-30,x+15,f-30,1,1,68);
  ht_limb(ht_scene,x-18,f-4,x+18,f-4,1,1,180);
  ht_limb(ht_scene,x+7,f-25,x+8,f-8,1,1,153);
  ht_limb(ht_scene,996-cx,f-42,996-cx,f-10,2,2,201);
  ht_limb(ht_scene,996-cx,f-10,991-cx,f-5,2,2,201);
  for(int n=0;n<3;++n){int at=930+n*22;
   ht_limb(ht_scene,at-cx,f-2,at+16-cx,f-2,1,1,106);
   ht_limb(ht_scene,at+9-cx,f-4,at+7-cx,f-1,0,0,215);}
  ht_checkpoint();
 }
}
/* Traceable wall clips and inward arrows. This exterior line disappears
 * behind the tower at x1008; no invented exposed span crosses its channel.
 * The terrace end rejoins the existing lamp feed, on support nine. */
static void ht_settlement_returns(const ht_game *g){
 if(g->level!=9 || g->door_stage)return;
 int cx=g->camera/256,cy=g->camera_y/256;
 static const int16_t street[][2]={{1008,106},{949,106},{949,135},{775,135},
  {744,125},{554,125},{500,137},{432,165},{341,165},{341,181},{284,181}};
 for(unsigned n=1;n<sizeof(street)/sizeof(street[0]);++n){
  int ax=street[n-1][0],ay=street[n-1][1],bx=street[n][0],by=street[n][1];
  if(!ht_settlement_visible(g,ht_min(ax,bx)-3,ht_max(ax,bx)+3))continue;
  ht_limb(ht_scene,ax-cx,ay-cy,bx-cx,by-cy,1,1,229);
  for(int clip=1;clip<4;++clip){int x=ax+(bx-ax)*clip/4,y=ay+(by-ay)*clip/4;
   ht_limb(ht_scene,x-cx,y-2-cy,x-cx,y+2-cy,1,1,111);}
  ht_checkpoint();
 }
 if(ht_settlement_visible(g,554,744)){
  ht_limb(ht_scene,716-cx,125-cy,716-cx,138-cy,1,1,229);
  ht_mechanism_arrow(614-cx,125-cy,600-cx);
 }
 if(ht_settlement_visible(g,284,341))ht_mechanism_arrow(326-cx,181-cy,311-cx);
 if(ht_settlement_visible(g,HT_PUZZLE_FIRST-81,HT_PUZZLE_FIRST+2*HT_PUZZLE_SPACING)){
  int f=ht_surface_at(g,9,HT_PUZZLE_FIRST)-cy;
  ht_limb(ht_scene,HT_PUZZLE_FIRST-80-cx,f-8,HT_PUZZLE_FIRST-cx,f-8,1,1,229);
  ht_limb(ht_scene,HT_PUZZLE_FIRST-80-cx,f-8,HT_PUZZLE_FIRST-80-cx,f-45,1,1,229);
  for(int n=0;n<3;++n){int x=HT_PUZZLE_FIRST+n*HT_PUZZLE_SPACING-cx;
   ht_limb(ht_scene,x,f-8,x,f-28,1,1,229);
   ht_mechanism_arrow(x-12,f-8,x-29);
  }
  ht_checkpoint();
 }
}
