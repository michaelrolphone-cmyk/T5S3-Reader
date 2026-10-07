/* Chapter V: the live ferry begins at its existing far stop. This examination
 * only takes up its mooring line; normal boarding/rowing owns the next step. */
#define HT_FERRY_STUDY 1
#define HT_FERRY_CALL_TICKS 96
#define HT_FERRY_REACH_TICKS 32
#define HT_FERRY_PULL_SPEED 192
static bool ht_ferry_near(const ht_game *g) {
 const ht_mechanics *m=ht_mech(g);
 return g->level==4 && !g->traversal.ferry_retrieved && !g->traversal.mode && g->grounded &&
  g->x>=(m->water_left-28)*256 && g->x<=(m->water_left-7)*256 &&
  ht_abs(g->y-ht_surface_at(g,0,g->x/256)*256)<=256;
}
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;int8_t direction;} ht_ferry_state;
static ht_ferry_state ht_ferry;
static bool ht_ferry_begin(void) {
 if(!ht_ferry_near(&ht))return false;
 ht_ferry=(ht_ferry_state){true,false,0,0,0};
 ht_ferry.stage=(uint8_t)(ht.traversal.boat_x<ht_mech(&ht)->boat_right*256?2:0);
 ht.traversal.ferry_line_taken=true;
 ht.vx=ht.vy=0;ht.facing=1;ht.coyote=ht.jump_buffer=0;return true;
}
static bool ht_ferry_action(ht_ferry_state *s) {
 if(!s->active || s->paused)return false;
 if(s->stage==0 || s->stage==2){++s->stage;s->tick=0;s->direction=0;return true;}
 return false;
}
static bool ht_ferry_step(ht_ferry_state *s) {
 if(!s->active || s->paused)return false;
 if(s->stage==1){if(s->tick<HT_FERRY_CALL_TICKS)++s->tick;if(s->tick==HT_FERRY_CALL_TICKS)s->stage=2;return true;}
 if(s->stage!=3)return false;
 if(s->tick<HT_FERRY_REACH_TICKS){++s->tick;return true;}
 if(s->direction>=0)return false;
 /* Fixed work per simulation step; 312 pulls at most, no detached boat copy. */
 int left=ht_mech(&ht)->boat_left*256;
 ht.traversal.boat_x=ht_max(left,ht.traversal.boat_x-HT_FERRY_PULL_SPEED);
 ht.traversal.boat_vx=0;
 s->tick=(uint16_t)(HT_FERRY_REACH_TICKS+(s->tick-HT_FERRY_REACH_TICKS+1)%48);
 if(ht.traversal.boat_x==left){ht.traversal.ferry_retrieved=true;s->stage=4;s->tick=0;s->direction=0;}
 return true;
}
static const char *ht_ferry_label(const ht_ferry_state *s) {
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "AT FIRST I THOUGHT SOMEONE WAS SITTING IN IT.";
 if(s->stage==1)return "I RAISED MY HAND, CALLED, AND WAITED.";
 if(s->stage==2)return "NOTHING LIFTED ITS HEAD.";
 if(s->stage==3 && s->tick<HT_FERRY_REACH_TICKS)return "I TOOK THE ROPE IN BOTH HANDS.";
 if(s->stage==3)return "THE PALE HAND BECAME AN OAR. THE SHOULDERS, CANVAS.";
 return "BY THE TIME THE BOAT REACHED ME, THE LIGHT WAS NEARLY GONE.";
}
/* Shared world contacts: both the rope renderer and the hands use this grip. */
static ht_joint ht_ferry_grip(const ht_game *g,const ht_ferry_state *s) {
 int pull=s->stage==3 && s->tick>=HT_FERRY_REACH_TICKS?(s->tick-HT_FERRY_REACH_TICKS)%48:0;
 int bend=pull<24?pull:48-pull;
 return ht_joint_at(g->x/256+7-bend/8,g->y/256-19);
}
static ht_person_pose ht_ferry_pose(const ht_game *g,const ht_ferry_state *s) {
 ht_game actor=*g;actor.vx=actor.vy=0;actor.stride=0;actor.facing=1;actor.landing_ticks=0;
 int cx=g->camera/256,cy=g->camera_y/256,x=g->x/256-cx,feet=g->y/256-cy;
 ht_person_pose p=ht_person_pose_at(x,feet,&actor);
 if(s->stage==1 && s->tick<40){p.hand[0]=ht_joint_at(x+7,feet-31);p.elbow[0]=1;}
 if(s->stage>=3){
  ht_joint grip=ht_ferry_grip(g,s);int q=s->stage==3?ht_min(s->tick,HT_FERRY_REACH_TICKS):HT_FERRY_REACH_TICKS;
  for(int i=0;i<2;++i){p.hand[i].x+=(grip.x-cx-p.hand[i].x)*q/HT_FERRY_REACH_TICKS;
   p.hand[i].y+=(grip.y-cy+i-p.hand[i].y)*q/HT_FERRY_REACH_TICKS;p.elbow[i]=i?1:-1;}
 }
 return p;
}
static void ht_ferry_details(const ht_game *g,const ht_ferry_state *s) {
 int cx=g->camera/256,cy=g->camera_y/256;
 int x=g->traversal.boat_x/256-cx,y=ht_boat_top(g)/256-cy;
 int post=ht_mech(g)->water_left-15,py=ht_surface_at(g,0,post)-cy;
 ht_limb(ht_scene,post-cx,py,post-cx+1,py-20,2,1,225);
 ht_joint grip=ht_ferry_grip(g,s);
 int ax=post-cx,ay=py-16;
 if(s->stage>=3 && (s->stage==4 || s->tick>=HT_FERRY_REACH_TICKS)){
  ht_limb(ht_scene,ax,ay,grip.x-cx,grip.y-cy,0,0,172);ax=grip.x-cx;ay=grip.y-cy;
 }
 int bow=x-ht_boat_half(g)+2,water=ht_mech(g)->boat_deck+10-cy;
 ht_limb(ht_scene,ax,ay,(ax+bow)/2,water+2,0,0,170);
 ht_limb(ht_scene,(ax+bow)/2,water+2,bow,y-3,0,0,170);

}
static void ht_ferry_study_render(const ht_game *game,const ht_ferry_state *s) {
 ht_game v=*game;v.camera=250*256;v.camera_y=65*256;v.intimacy=0;v.vista=256;v.drop_zoom=0;
 v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);
 ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 ht_ferry_details(&v,s);ht_person_pose p=ht_ferry_pose(&v,s);ht_person_draw(&p);
 ht_world_scale=256;ht_rect(ht_scene,10,226,460,35,255);
 const char *label=ht_ferry_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A CALL  START JOURNAL  X RETURN":s->stage==2?"A TAKE ROPE  START JOURNAL  X RETURN":s->stage==3?"HOLD LEFT TO PULL  PAUSE HOLD  X RETURN":s->stage==4?"A RETURN TO BANK - THEN BOARD":"PAUSE HOLD  START JOURNAL  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_ferry_prompt(const ht_game *g){
 if(!ht_ferry_near(g))return;
 ht_rect(ht_scene,66,33,348,19,255);ht_text_color(74,38,"A EXAMINE THE FAR-BANK BOAT AND MOORING",1,0);
}
