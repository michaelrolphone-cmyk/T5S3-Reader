/* Chapter VI: the parked cage may be examined before balancing the hoist.
 * Only the existing page-17 discovery is recorded; snapshots enter the cage
 * without moving the live body through its unsolved exit or changing loads. */
#define HT_FLOORBOARD_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_floorboard_state;
static ht_floorboard_state ht_floorboard;
static bool ht_floorboard_near(const ht_game *g){return g->level==5 && !g->traversal.mode && ht_evidence_near(g)==2;}
static void ht_floorboard_begin(void){ht_floorboard=(ht_floorboard_state){true,false,0,0};}
static unsigned ht_floorboard_limit(unsigned stage){return stage==1?64:stage==3?96:stage==5?96:stage==7?64:0;}
static bool ht_floorboard_step(ht_floorboard_state *s){
 if(!s->active || s->paused || !ht_floorboard_limit(s->stage))return false;
 if(++s->tick==ht_floorboard_limit(s->stage)){++s->stage;s->tick=0;}return true;
}
static bool ht_floorboard_action(ht_floorboard_state *s){
 if(!s->active || s->paused || (s->stage&1) || s->stage>=8)return false;
 ++s->stage;s->tick=0;return true;
}
static const char *ht_floorboard_label(const ht_floorboard_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "I WAS AFRAID OF THE CAGE.";
 if(s->stage==1)return "MY SISTER HAD PUT SIX PEOPLE INTO IT. OR SEVEN.";
 if(s->stage==2)return "A TORN NOTE WAS TRAPPED BENEATH A LOOSE FLOORBOARD.";
 if(s->stage==3)return "I KNELT ON THE CAGE FLOOR.";
 if(s->stage==4)return "AT THE TORN EDGE: WHEN I AM NO LONGER WITH THEM--";
 if(s->stage==5)return "I LOOKED THROUGH THE GAPS BETWEEN THE BOARDS.";
 if(s->stage==6)return "ALL THAT CLIMBING. I MIGHT HAVE BEEN WALKING AWAY.";
 if(s->stage==7)return s->tick<24?"I STOOD TOO QUICKLY.":"MY SHOULDER STRUCK THE CAGE FRAME.";
 return "THE PAIN GAVE ME SOMETHING I COULD PUT A HAND OVER.";
}
static ht_person_pose ht_floorboard_pose(const ht_game *game,const ht_floorboard_state *s){
 ht_game v=*game;unsigned q=s->stage==1?ht_min(s->tick,64)*256/64:s->stage>=2?256:0;
 int wx=game->x/256+(HT_GOAL+3-game->x/256)*(int)q/256;
 int base=ht_level_land(5)[9].top,cy=v.camera_y/256,cx=v.camera/256;
 v.x=wx*256;v.y=base*256;v.vx=s->stage==1?248:0;v.vy=0;v.grounded=true;v.facing=1;v.landing_ticks=0;v.stride=s->tick*64;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(wx-cx,base-cy,&v);
 if(s->stage==1){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=(p.foot[i].x+wx-cx)/2;}
 unsigned kneel=s->stage==3?ht_min(s->tick,48)*256/48:s->stage>=4 && s->stage<=6?256:0;
 if(s->stage==7)kneel=(24-ht_min(s->tick,24))*256/24;
 if(s->stage>=3){
  int shift=s->stage==7?(int)ht_min(s->tick,24)*21/24:s->stage>=8?21:0;
  wx+=shift;p.hip=ht_joint_at(wx-cx,base-13+(int)kneel*8/256-cy);
  p.shoulder=ht_joint_at(wx+2-cx,base-22+(int)kneel*13/256-cy);
  p.foot[0]=ht_joint_at(wx-5-cx,base-cy);p.foot[1]=ht_joint_at(ht_min(wx+3,HT_GOAL+24)-cx,base-cy);
  p.hand[0]=ht_joint_at(p.shoulder.x-4,p.shoulder.y+8);p.hand[1]=ht_joint_at(p.shoulder.x+5,p.shoulder.y+7);
  if(s->stage>=8 || (s->stage==7 && s->tick>=24)){
   /* The right shoulder shares the inner edge of the existing right post. */
   p.hand[0]=p.shoulder;p.hand[1]=ht_joint_at(p.shoulder.x-1,p.shoulder.y+7);
  }
 }
 return p;
}
/* A downward view of this cage's boards: the paper is held by a lifted edge,
 * and only one pale projecting stone interrupts the dark shaft below. */
static void ht_floorboard_insert(const ht_floorboard_state *s){
 ht_rect(ht_scene,0,0,480,226,242);
 bool gap=s->stage>=5;
 int spread=gap?(s->stage==5?(int)ht_min(s->tick,96)*29/96:29):0;
 for(int side=0;side<3;++side){
  int x=side==0?-12:side==1?165:339+spread,w=side==0?161:side==1?146-spread:153;
  ht_rect(ht_scene,x,0,w,226,side==1?113:145);
  for(int n=0;n<12;++n){int xx=x+9+n*12;ht_limb(ht_scene,xx,0,xx-7,226,0,0,side==1?140:163);}
  for(int y=12;y<225;y+=23)ht_limb(ht_scene,x+3,y,x+w-4,y-7,0,0,side==1?94:128);
 }
 ht_triangle(ht_scene,313,123,331,112,336,135,80);ht_triangle(ht_scene,313,123,336,135,319,142,131);
 ht_limb(ht_scene,10,207,466,207,5,5,190);
 if(!gap){
  /* Torn edge is geometry, not a completed or invented missing sentence. */
  ht_rect(ht_scene,171,28,138,161,43);
  for(int n=0;n<14;++n)ht_triangle(ht_scene,171+n*10,186,181+n*10,186,175+n*10,192+n%3*2,43);
  ht_text_color(181,39,"THREE CHILDREN",1,187);ht_text_color(181,53,"TWO ADULTS",1,187);ht_text_color(181,67,"ONE OLD MAN",1,187);
  ht_text_color(181,86,"SIX ARE STILL",1,187);ht_text_color(181,100,"WITH ME.",1,187);
  ht_text_color(181,124,"SEVEN,",1,203);ht_text_color(181,138,"INCLUDING YOU.",1,203);
  ht_text_color(179,160,"WHEN I AM NO LONGER",1,187);ht_text_color(179,173,"WITH THEM--",1,187);
  /* The board's uneven edge still pins the margin. */
  ht_rect(ht_scene,163,21,13,175,112);ht_limb(ht_scene,163,21,175,24,1,1,67);
  ht_limb(ht_scene,97,238,145,145,11,7,231);ht_limb(ht_scene,145,145,163,119,6,2,223);
  ht_limb(ht_scene,385,238,338,195,12,7,232);ht_limb(ht_scene,338,195,303,182,5,2,224);
 }else{
  ht_limb(ht_scene,63,244,101,171,14,8,231);ht_limb(ht_scene,101,171,126,148,7,3,223);
  ht_limb(ht_scene,423,244,384,176,14,8,231);ht_limb(ht_scene,384,176,357,148,7,3,223);
 }
}
static void ht_floorboard_study_render(const ht_game *game,const ht_floorboard_state *s){
 ht_game v=*game;v.camera=(HT_GOAL-237)*256;v.camera_y=(ht_level_land(5)[9].top-187)*256;
 v.intimacy=256;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);
 ht_world_scale=ht_scene_scale(&v);ht_person_pose p=ht_floorboard_pose(&v,s);ht_person_draw(&p);
 if(s->stage>=2 && s->stage<4){
  int x=HT_GOAL+3-v.camera/256,y=ht_level_land(5)[9].top-v.camera_y/256;
  ht_triangle(ht_scene,x-4,y-2,x+5,y-5,x+7,y-1,36);
 }
 ht_world_scale=256;
 if(s->stage>=4 && s->stage<=6)ht_floorboard_insert(s);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_floorboard_label(s);
 ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A ENTER CAGE  START JOURNAL  X RETURN":s->stage==2?"A KNEEL  START NOTE  X RETURN":s->stage==4?"A LOOK DOWN  START NOTE  X RETURN":s->stage==6?"A STAND  START NOTE  X RETURN":s->stage==8?"A READ NOTE  START NOTE  X RETURN":"PAUSE HOLD  START JOURNAL  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
