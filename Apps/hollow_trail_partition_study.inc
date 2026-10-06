#define HT_PARTITION_STUDY 1
typedef struct {bool active,paused;uint16_t tick;} ht_partition_state;
static ht_partition_state ht_partition;
static bool ht_partition_earned(const ht_game *before,const ht_game *after){
 return before->level==6 && after->level==6 && !before->puzzle.stage && after->puzzle.stage==1 && !after->puzzle.solved && ht_puzzle_near(after)==3;
}
static void ht_partition_begin(void){ht_partition=(ht_partition_state){true,false,0};}
static bool ht_partition_step(ht_partition_state *s){if(!s->active || s->paused || s->tick>=HT_PARTITION_TICKS)return false;++s->tick;return true;}
static const char *ht_partition_label(const ht_partition_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->tick<32)return "A FALLING DROP CROSSED THE BEAM AND FLASHED.";
 if(s->tick<64)return "AT THE UPPER RECEIVER, A CATCH LIFTED.";
 if(s->tick<184)return "COLD AIR TOUCHED ONE ROW OF LEAVES, THEN ANOTHER.";
 if(s->tick<352)return "DROPS JOINED AND SLID DOWN THE PANE.";
 return "THROUGH IT, I COULD SEE THE PATH ONWARD.";
}
static void ht_partition_study_render(const ht_game *game,const ht_partition_state *s){
 ht_game v=*game;v.camera=(HT_GOAL-330)*256;v.camera_y=(ht_level_land(6)[9].top-190)*256;
 v.vx=v.vy=0;v.landing_ticks=0;
 v.intimacy=0;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.partition_view_active=true;v.partition_view_tick=s->tick;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);
 ht_render_scene_from(&v,true);ht_world_scale=256;
 ht_rect(ht_scene,7,226,466,35,255);const char *label=ht_partition_label(s);
 ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=s->tick==HT_PARTITION_TICKS?"A RETURN TO MIRRORS  START NOTE  X RETURN":"START NOTE  PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
