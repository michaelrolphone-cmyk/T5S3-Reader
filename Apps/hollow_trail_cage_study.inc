/* Optional empty-cage trial after the conserved six weights have truly won.
 * A fixed 96-step stroke (32 world pixels) bounds every movement/recovery.
 * No action writes live game, evidence, puzzle, boarding or descent state. */
#define HT_EMPTY_CAGE_STUDY 1
#define HT_CAGE_STROKE 96u
enum {HT_CAGE_REACH,HT_CAGE_CONTROL,HT_CAGE_CABLE,HT_CAGE_RETURN};
typedef struct {bool active,paused;uint8_t stage,travel,cycles;uint16_t tick;int8_t direction;} ht_cage_state;
static ht_cage_state ht_cage;
static bool ht_empty_cage_near(const ht_game *g){
 const uint8_t *v=g->puzzle.value;
 return g->level==5 && !g->traversal.mode && ht_puzzle_near(g)==3 &&
        g->puzzle.solved && g->puzzle.opening==48 && v[0]+v[1]+v[2]==6 &&
        v[0] && v[1] && v[2] && v[0]*3==v[2]*2;
}
static void ht_cage_begin(void){ht_cage=(ht_cage_state){true,false,0,0,0,0,0};}
static void ht_cage_control(ht_cage_state *s,int8_t direction){
 if(!s->active || s->paused || s->stage==HT_CAGE_REACH || s->stage==HT_CAGE_RETURN){s->direction=0;return;}
 if(direction && s->stage==HT_CAGE_CABLE){s->stage=HT_CAGE_CONTROL;s->tick=0;}
 s->direction=direction;
}
static void ht_cage_return(ht_cage_state *s){
 if(!s->active)return;
 s->paused=false;s->direction=0;s->tick=0;
 if(s->travel)s->stage=HT_CAGE_RETURN;else s->active=false;
}
static void ht_cage_action(ht_cage_state *s){
 if(!s->active || s->paused || s->stage==HT_CAGE_REACH || s->stage==HT_CAGE_RETURN)return;
 if(s->direction || s->travel){s->direction=0;return;}
 s->stage=s->stage==HT_CAGE_CABLE?HT_CAGE_CONTROL:HT_CAGE_CABLE;s->tick=0;
}
static bool ht_cage_step(ht_cage_state *s){
 if(!s->active || s->paused)return false;
 if(s->stage==HT_CAGE_REACH){if(++s->tick==64){s->stage=HT_CAGE_CONTROL;s->tick=0;}return true;}
 if(s->stage==HT_CAGE_CABLE){if(s->tick==96)return false;++s->tick;return true;}
 int direction=s->stage==HT_CAGE_RETURN?-1:s->direction;
 if(direction>0 && s->travel<HT_CAGE_STROKE){++s->travel;return true;}
 if(direction<0 && s->travel){
  --s->travel;
  if(!s->travel){if(s->cycles<2)++s->cycles;if(s->stage==HT_CAGE_RETURN)s->active=false;}
  return true;
 }
 return false;
}
static const char *ht_cage_label(const ht_cage_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->stage==HT_CAGE_REACH)return "I TESTED THE HANDLE. THE WOOL PRESSED INTO MY PALM.";
 if(s->stage==HT_CAGE_RETURN)return "I RAISED THE EMPTY CAGE BACK TO THE LANDING.";
 if(s->stage==HT_CAGE_CABLE)return s->tick<96?"I WATCHED THE CABLE ALL THE WAY UP TO THE PULLEY.":"I SEARCHED FOR LOOSE WIRES.";
 if(s->travel==HT_CAGE_STROKE)return "THE EMPTY CAGE HELD A LITTLE BELOW THE LANDING.";
 if(s->direction>0)return "I LOWERED THE EMPTY CAGE A LITTLE.";
 if(s->direction<0 && s->travel)return "I RAISED IT AGAIN.";
 if(s->travel)return "I STOPPED IT.";
 return s->cycles>=2?"THEN I DID IT ONCE MORE.":s->cycles?"THE EMPTY CAGE WAS BACK AT THE LANDING.":"ONE BLOCK REMAINED ON THE BRAKE.";
}
static ht_person_pose ht_cage_pose(const ht_game *game,const ht_cage_state *s){
 ht_game v=*game;unsigned q=s->stage==HT_CAGE_REACH?ht_min(s->tick,32)*256/32:256;
 int target=HT_CAGE_HANDLE_X-2,wx=game->x/256+(target-game->x/256)*(int)q/256;
 v.x=wx*256;v.y=ht_surface_at(&v,9,wx)*256;v.vx=s->stage==HT_CAGE_REACH && s->tick<32?248:0;
 v.vy=0;v.facing=1;v.grounded=true;v.landing_ticks=0;v.stride=s->tick*72;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(wx-v.camera/256,(v.y-v.camera_y)/256,&v);
 if(v.vx){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=(p.foot[i].x+wx-v.camera/256)/2;}
 unsigned reach=s->stage==HT_CAGE_REACH?(unsigned)ht_clamp((int)s->tick-32,0,32)*256/32:256;
 ht_joint c=ht_empty_cage_grip(&v);c.x-=v.camera/256;c.y-=v.camera_y/256;
 p.hand[1]=ht_grip_mix(p.hand[1],c,reach);return p;
}
static void ht_cage_study_render(const ht_game *game,const ht_cage_state *s){
 ht_game v=*game;v.camera=(HT_PUZZLE_FIRST-58)*256;v.camera_y=(ht_level_land(5)[9].top-180)*256;
 v.intimacy=v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.cage_view_drop=s->travel/3;v.cage_view_travel=s->travel;
 if(s->stage==HT_CAGE_CABLE){
  /* Look upward along this exact cable, keeping the actor at the handle. */
  unsigned q=ht_min(s->tick,96);v.camera=(HT_GOAL-244)*256;
  v.camera_y=(ht_level_land(5)[9].top-204-(int)q/4)*256;v.intimacy=128;
 }
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);
 ht_world_scale=ht_scene_scale(&v);ht_person_pose p=ht_cage_pose(&v,s);ht_person_draw(&p);ht_world_scale=256;
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_cage_label(s);
 ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=s->stage==HT_CAGE_REACH || s->stage==HT_CAGE_RETURN?"START JOURNAL  PAUSE HOLD  X RETURN":
   s->direction || s->travel?"LEFT RAISE  RIGHT LOWER  A STOP  X RETURN":
   "LEFT RAISE  RIGHT LOWER  A CABLE  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
