typedef struct {bool active,paused;int8_t direction;uint8_t stage,tick,overlap;} ht_distribution_state;
static ht_distribution_state ht_distribution;
static bool ht_distribution_near(const ht_game *g) {
 return g->level==2 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==2;
}
static void ht_distribution_begin(void){ht_distribution=(ht_distribution_state){true,false,0,0,0,0};}
static bool ht_distribution_step(ht_distribution_state *s){
 if(!s->active || s->paused)return false;
 if(!s->stage && s->tick<128){++s->tick;return true;}
 if(s->stage){int old=s->overlap;s->overlap=(uint8_t)ht_clamp(old+s->direction*2,0,64);return old!=s->overlap;}
 return false;
}
static bool ht_distribution_take(ht_distribution_state *s){
 if(!s->active || s->paused || s->stage || s->tick<128)return false;
 s->stage=1;s->overlap=0;s->direction=0;return true;
}
static const char *ht_distribution_label(const ht_distribution_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return s->tick<80?"THE CAN HANDLES WERE TIED TOGETHER.":s->tick<128?"I TOUCHED ONE CAN. THE WHOLE ROW MOVED.":"TWO LISTS, ROLLED INSIDE THE LAST CAN.";
 return s->overlap>48?"THE INNER LIST WAS DATED BEFORE HER ARRIVAL.":"LEFT: AN EMPTY EIGHTH LINE. RIGHT: MY INITIAL.";
}
static void ht_distribution_initial(int x,int y){
 ht_limb(ht_scene,x+14,y+94,x+17,y+90,1,1,231);
 ht_limb(ht_scene,x+17,y+90,x+22,y+94,1,0,231);
}
static void ht_distribution_sheet(int x,int y,bool inner){
 ht_triangle(ht_scene,x,y,x+112,y+3,x+108,y+115,inner?74:44);
 ht_triangle(ht_scene,x,y,x+108,y+115,x-3,y+109,inner?74:44);
 ht_rect(ht_scene,x+8,y+13,75,1,143); /* date strokes, no fabricated numerals */
 for(int row=0;row<8;++row){
  int yy=y+28+row*10;
  ht_rect(ht_scene,x+7,yy,93,1,133);
  if(row<7)for(int n=0;n<4;++n)
   ht_limb(ht_scene,x+13+n*16,yy-3,x+20+n*16,yy-5+(row+n)%3,1,0,188);
 }
 if(inner){
  /* The unnamed narrator's initial is an incomplete ink mark, not an
   * invented name or printed letter. Its presence distinguishes line eight. */
  ht_distribution_initial(x,y);
 }
}
static void ht_distribution_study_render(const ht_game *game,const ht_distribution_state *s){
 ht_native_active=ht_camera_mode==HT_CAMERA_NATIVE && ht_native_a;
 ht_native_foreground_half_y=false;ht_scene=ht_native_active?ht_native_a:ht_scene_low;ht_world_scale=256;
 for(int y=0;y<HT_H;++y){ht_rect(ht_scene,0,y,HT_W,1,110+y/7);if((y&15)==15)ht_checkpoint();}
 if(!s->stage){
  ht_game v=*game;v.camera=(HT_DISTRIBUTION_X-260)*256;v.camera_y=(ht_distribution_floor()-190)*256;
  ht_world_scale=384;
  for(int sx=-40;sx<HT_W+40;sx+=8){
   int yy=ht_surface_at(&v,7,v.camera/256+sx)-v.camera_y/256;
   ht_rect(ht_scene,sx,yy,8,ht_max(0,HT_H+80-yy),155);
  }
  ht_distribution_room(&v,s->tick<80?48:s->tick-80);
  /* Only the inspection view approaches the can; live position stays frozen. */
  int target=HT_DISTRIBUTION_X-6,from=game->x/256;
  v.x=(from+(target-from)*(int)ht_min(s->tick,80)/80)*256;
  v.y=ht_surface_at(&v,7,v.x/256)*256;v.vx=v.vy=0;v.facing=1;v.ticks=0;
  ht_person_pose p=ht_person_pose_at(v.x/256-v.camera/256,(v.y-v.camera_y)/256,&v);
  if(s->tick>=80){p.hand[0]=ht_joint_at(HT_DISTRIBUTION_X-v.camera/256+ht_distribution_can_offset(s->tick-80),p.shoulder.y-2);p.elbow[0]=1;}
  ht_person_draw(&p);
 }else{
  /* The low wall and two knees support the sheets; can rests between feet. */
  ht_rect(ht_scene,81,197,324,20,168);
  ht_limb(ht_scene,112,214,144,170,22,19,217);ht_disk(ht_scene,144,170,19,217);
  ht_limb(ht_scene,367,214,329,172,22,19,227);ht_disk(ht_scene,329,172,19,227);
  ht_rect(ht_scene,218,190,30,33,210);ht_rect(ht_scene,224,181,17,11,218);ht_rect(ht_scene,228,184,9,6,111);
  int left=95+s->overlap,right=267-s->overlap;
  ht_distribution_sheet(left,67,false);ht_distribution_sheet(right,70,true);
  /* Both hands stay attached to their own sheet while bringing them together. */
  for(int side=0;side<2;++side){
   int paper=side?right+98:left+8,wrist=side?378:95;
   ht_limb(ht_scene,wrist,218,paper,169,6,4,237);
   ht_disk(ht_scene,paper,166,4,244);
   ht_limb(ht_scene,paper,167,paper+(side?-5:5),158,2,1,244);
  }
 }
 ht_world_scale=256;ht_rect(ht_scene,12,226,456,35,255);
 const char *label=ht_distribution_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,64);
 const char *keys=s->stage?"L/R COMPARE  A READ  START NOTE  X RETURN":"A TAKE LISTS  START NOTE  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,100);ht_vignette();
}
