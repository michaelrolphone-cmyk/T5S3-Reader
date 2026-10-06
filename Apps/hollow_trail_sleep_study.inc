#define HT_SLEEP_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_sleep_state;
static ht_sleep_state ht_sleep;
static bool ht_sleep_near(const ht_game *g){return g->level==6 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==0;}
static void ht_sleep_begin(void){ht_sleep=(ht_sleep_state){true,false,0,0};}
static bool ht_sleep_step(ht_sleep_state *s){
 if(!s->active || s->paused)return false;
 if(s->stage==1){if(++s->tick==176)s->stage=2;return true;}
 if(s->stage==3){if(++s->tick==128)s->stage=4;return true;}
 return false;
}
static bool ht_sleep_action(ht_sleep_state *s){
 if(!s->active || s->paused || (s->stage!=0 && s->stage!=2))return false;
 s->stage=s->stage==0?1:3;s->tick=0;return true;
}
static const char *ht_sleep_label(const ht_sleep_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "SEVEN SLEEPING PLACES. THE LOCK WAS INSIDE.";
 if(s->stage==1)return s->tick<112?"I LIFTED THE WEDGE.":"THE BRIGHT STRIP NARROWED. LEAVES LOST THEIR SHINE.";
 if(s->stage==2)return "I CAUGHT THE DOOR.";
 if(s->stage==3)return "I PUSHED THE WEDGE BACK.";
 return "THE KEY WAS GONE. THE FLOOR BORE A WIDE SCRAPE.";
}
static ht_person_pose ht_sleep_pose(const ht_game *game,const ht_sleep_state *s){
 ht_game v=*game;int from=game->x/256,target=HT_SLEEP_X+11,at=from,crouch=0,close=ht_sleep_closure(s->stage,s->tick);
 if(s->stage==1){
  at=from+(target-from)*(int)ht_min(s->tick,48)/48;
  if(s->tick>=48 && s->tick<80)crouch=(int)(s->tick-48)*256/32;
  else if(s->tick>=80 && s->tick<112)crouch=256-(int)(s->tick-80)*256/32;
  else if(s->tick>=112)at=target+(int)(s->tick-112)*18/64;
 }else if(s->stage==2)at=target+18;
 else if(s->stage>=3){
  at=s->stage==3 && s->tick<64?target+18-(int)s->tick*18/64:target;
  if(s->stage==3 && s->tick>=64 && s->tick<96)crouch=(int)(s->tick-64)*256/32;
  else if(s->stage==3 && s->tick>=96)crouch=256-(int)(s->tick-96)*256/32;
 }
 v.x=at*256;v.y=ht_surface_at(&v,1,at)*256;v.vx=s->stage==1 && s->tick<48?(target>=from?248:-248):0;v.vy=0;v.facing=at>HT_SLEEP_X+8?-1:1;v.grounded=true;v.stride=(unsigned)s->tick*128;v.ticks=s->tick;
 int x=at-v.camera/256,f=(v.y-v.camera_y)/256;ht_person_pose p=ht_person_pose_at(x,f,&v);
 if(v.vx){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=x+(p.foot[i].x-x)*3/4;}
 if(crouch){p.hip=ht_joint_at(x,f-13+8*crouch/256);p.shoulder=ht_joint_at(x-2,f-23+13*crouch/256);p.foot[0]=ht_joint_at(x-3,f);p.foot[1]=ht_joint_at(x+3,f);p.hand[0]=ht_joint_at(x-3,p.shoulder.y+9);p.hand[1]=ht_joint_at(x+3,p.shoulder.y+7);}
 else if(s->stage==2 || (s->stage==1 && s->tick>=112) || (s->stage==3 && s->tick<64)){
  p.hand[1]=ht_joint_at(HT_SLEEP_X+3+close*25/64-v.camera/256,ht_sleep_floor()-v.camera_y/256-22);p.elbow[1]=1;
 }
 return p;
}
static void ht_sleep_study_render(const ht_game *game,const ht_sleep_state *s){
 ht_game v=*game;v.camera=(HT_SLEEP_X-108)*256;v.camera_y=(ht_sleep_floor()-190)*256;v.intimacy=0;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.sleep_view_active=true;v.sleep_view_stage=s->stage;v.sleep_view_tick=s->tick;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);
 ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);ht_person_pose p=ht_sleep_pose(&v,s);ht_person_draw(&p);
 if((s->stage==1 && s->tick>=80) || s->stage==2 || (s->stage==3 && s->tick<96))ht_triangle(ht_scene,p.hand[0].x-3,p.hand[0].y,p.hand[0].x+3,p.hand[0].y,p.hand[0].x+2,p.hand[0].y-3,179);
 ht_world_scale=256;ht_rect(ht_scene,5,226,470,35,255);
 const char *label=ht_sleep_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A LIFT WEDGE  START NOTE  X RETURN":s->stage==2?"A WEDGE IT OPEN  START NOTE  X RETURN":s->stage==4?"A READ NOTE  START NOTE  X RETURN":"START NOTE PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
