/* One cage floor/frame for the actual upper exit and the earned descent.
 * All coordinates use the existing support-nine landing and RUN handle. */
#define HT_CAGE_HANDLE_X (HT_PUZZLE_FIRST+3*HT_PUZZLE_SPACING)
static int ht_cage_floor(int center,int base,int tilt,int x){return base+(x-center)*tilt/12;}
static void ht_cage_frame(int center,int base,int tilt){
 ht_rect(ht_scene,center-26,base-70,51,3,83);ht_rect(ht_scene,center-27,base-70,3,73,95);ht_rect(ht_scene,center+23,base-70,3,73,95);
 for(int x=center-24;x<center+24;++x){
  int floor=ht_cage_floor(center,base,tilt,x),local=x-center;
  if((local>=-7 && local<-5) || (local>=8 && local<10)){ht_rect(ht_scene,x,floor,1,5,240);continue;}
  ht_rect(ht_scene,x,floor,1,5,local>=-5 && local<8?105:137);
  if(local>=-5 && local<8 && !((local+240)%3))ht_rect(ht_scene,x,floor+1,1,2,106);
 }
}
static ht_joint ht_empty_cage_grip(const ht_game *g){
 int turn=(g->cage_view_travel/8)%3;
 return ht_joint_at(HT_CAGE_HANDLE_X+3+turn,ht_level_land(5)[9].top-30+turn);
}
static ht_joint ht_empty_cage_pulley(void){return ht_joint_at(HT_GOAL+3,ht_level_land(5)[9].top-104);}
static ht_joint ht_empty_cage_roof(unsigned drop){return ht_joint_at(HT_GOAL+3,ht_level_land(5)[9].top-70+(int)drop);}
static void ht_empty_cage_room(const ht_game *g){
 if(g->level!=5 || !g->puzzle.solved || g->puzzle.opening!=48)return;
 int x=HT_GOAL+3-g->camera/256,f=ht_level_land(5)[9].top-g->camera_y/256;
 if(x<-60-ht_view_margin() || x>HT_W+60+ht_view_margin())return;
 unsigned drop=ht_min(g->cage_view_drop,32);
 ht_joint pulley=ht_empty_cage_pulley(),roof=ht_empty_cage_roof(drop);
 /* The same fixed pulley pays out cable; the frame moves alone, empty. */
 ht_limb(ht_scene,pulley.x-g->camera/256,pulley.y-g->camera_y/256,x,roof.y-g->camera_y/256,1,1,90);
 ht_disk(ht_scene,x,f-104,7,113);ht_disk(ht_scene,x,f-104,3,190);
 ht_cage_frame(x,f+(int)drop,0);
}
