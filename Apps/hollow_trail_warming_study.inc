#define HT_WARMING_STUDY 1
typedef struct {bool active,paused;uint16_t tick;} ht_warming_state;
static ht_warming_state ht_warming;
static bool ht_warming_earned(const ht_game *before,const ht_game *after){
 return before->level==7 && after->level==7 && !before->puzzle.stage && after->puzzle.stage==1 && !after->puzzle.solved && ht_puzzle_near(after)==3;
}
static void ht_warming_begin(void){ht_warming=(ht_warming_state){true,false,0};}
static bool ht_warming_step(ht_warming_state *s){if(!s->active || s->paused || s->tick>=HT_WARMING_TICKS)return false;++s->tick;return true;}
static const char *ht_warming_label(const ht_warming_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->tick<96)return "I WAITED WITH MY PALM ON THE PIPE.";
 if(s->tick<224)return "IT WARMED BY DEGREES.";
 if(s->tick<HT_WARMING_TICKS)return "CONDENSATION CLEARED TOWARD THE GARDENS.";
 return "THE RETURN HELD PRESSURE. THE RIDGE STILL WAITED.";
}
static ht_person_pose ht_warming_pose(const ht_game *game,const ht_warming_state *s){
 ht_game v=*game;int to=HT_WARMING_TOUCH_X+9,from=game->x/256;
 v.x=(from+(to-from)*(int)ht_min(s->tick,96)/96)*256;v.y=ht_surface_at(&v,9,v.x/256)*256;
 v.vx=s->tick<96?-248:0;v.vy=0;v.facing=-1;v.grounded=true;v.stride=(unsigned)s->tick*96;v.ticks=s->tick;
 int x=v.x/256-v.camera/256;ht_person_pose p=ht_person_pose_at(x,(v.y-v.camera_y)/256,&v);
 if(s->tick<96){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=x+(p.foot[i].x-x)*3/4;}
 else {p.hand[0]=ht_joint_at(HT_WARMING_TOUCH_X-v.camera/256,ht_level_land(7)[9].top-v.camera_y/256-23);p.elbow[0]=1;}
 return p;
}
static void ht_warming_study_render(const ht_game *game,const ht_warming_state *s){
 ht_game v=*game;v.camera=(HT_WARMING_TOUCH_X-285)*256;v.camera_y=(ht_level_land(7)[9].top-190)*256;
 v.intimacy=0;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.warming_view_active=true;v.warming_view_tick=s->tick;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);
 ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);ht_person_pose p=ht_warming_pose(&v,s);ht_person_draw(&p);
 ht_world_scale=256;ht_rect(ht_scene,7,226,466,35,255);const char *label=ht_warming_label(s);
 ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=s->tick==HT_WARMING_TICKS?"A RETURN TO CONTROLS  START NOTE  X RETURN":"START NOTE  PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
