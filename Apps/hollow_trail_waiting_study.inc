typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_waiting_state;
static ht_waiting_state ht_waiting;
static bool ht_waiting_near(const ht_game *g){
 return g->level==4 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==1;
}
static void ht_waiting_begin(void){ht_waiting=(ht_waiting_state){true,false,0,0};}
static bool ht_waiting_step(ht_waiting_state *s){
 if(!s->active || s->paused || s->stage!=1)return false;
 if(s->tick<600)++s->tick;
 if(s->tick==600)s->stage=2;
 return true;
}
static bool ht_waiting_ring(ht_waiting_state *s){
 if(!s->active || s->paused || s->stage)return false;
 s->stage=1;s->tick=0;return true;
}
static const char *ht_waiting_label(const ht_waiting_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "A WALKING STICK HAD RESTED IN THAT GROOVE.";
 if(s->tick<72)return "THE BELL ROPE HAD A LOOP A CHILD COULD REACH.";
 if(s->tick<104)return "I PULLED IT ONCE.";
 if(s->tick<344)return "ITS ARC NARROWED UNTIL IT HUNG WHOLLY DARK.";
 return "NOTHING ELSE ANSWERED. I KEPT HOLD OF THE ROPE.";
}
static ht_person_pose ht_waiting_pose(const ht_game *game,const ht_waiting_state *s){
 ht_game v=*game;int cy=v.camera_y/256,cx=v.camera/256,f=ht_waiting_floor()-cy;
 int walk=s->stage?(int)ht_min(s->tick,72):0;
 v.x=(HT_WAITING_X-2+40*walk/72)*256;v.y=ht_surface_at(&v,4,v.x/256)*256;
 v.stride=(unsigned)walk*40u*256u/72u;v.vx=s->stage && s->tick<72?248:0;v.vy=0;v.facing=1;v.grounded=true;v.ticks=s->tick;
 ht_person_pose p=ht_person_pose_at(v.x/256-cx,(v.y-v.camera_y)/256,&v);
 if(s->stage && s->tick<72){
  ++p.hip.y;++p.shoulder.y;
  for(int i=0;i<2;++i)p.foot[i].x=v.x/256-cx+(p.foot[i].x-(v.x/256-cx))*3/4;
 }
 if(!s->stage){
  p.hip=ht_joint_at(HT_WAITING_X-2-cx,f-11);p.shoulder=ht_joint_at(HT_WAITING_X+1-cx,f-20);
  p.foot[0]=ht_joint_at(HT_WAITING_X+3-cx,f);p.foot[1]=ht_joint_at(HT_WAITING_X+5-cx,f);
  p.hand[0]=ht_joint_at(HT_WAITING_X+9-cx,f-23);p.elbow[0]=1;
  p.hand[1]=ht_joint_at(HT_WAITING_X-3-cx,f-10);
 }else if(s->tick>=72){
  p.hand[0]=ht_joint_at(HT_WAITING_X+48-cx,f-27+ht_waiting_pull(s->tick));p.elbow[0]=1;
 }
 return p;
}
static void ht_waiting_study_render(const ht_game *game,const ht_waiting_state *s){
 ht_game v=*game;v.camera=(HT_WAITING_X-207)*256;v.camera_y=(ht_waiting_floor()-184)*256;
 v.intimacy=128;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.waiting_view_active=s->stage!=0;v.waiting_view_tick=s->tick;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);
 ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 ht_person_pose p=ht_waiting_pose(&v,s);ht_person_draw(&p);
 ht_world_scale=256;ht_rect(ht_scene,10,226,460,35,255);
 const char *label=ht_waiting_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A PULL ONCE  START NOTE  X RETURN":s->stage==2?"A READ NOTE  START NOTE  X RETURN":"START NOTE  PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
