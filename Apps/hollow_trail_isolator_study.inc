#define HT_ISOLATOR_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_isolator_state;
static ht_isolator_state ht_isolator;
static bool ht_isolator_earned(const ht_game *before,const ht_game *after){
 return before->level==8 && after->level==8 && !before->puzzle.stage && after->puzzle.stage==1 && !after->puzzle.solved && ht_puzzle_near(after)==3;
}
static void ht_isolator_begin(void){ht_isolator=(ht_isolator_state){true,false,0,0};}
static bool ht_isolator_step(ht_isolator_state *s){
 if(!s->active || s->paused || s->stage==2)return false;
 if(!s->stage){if(s->tick>=HT_ISOLATOR_WAIT)return false;++s->tick;return true;}
 if(++s->tick==128)s->stage=2;
 return true;
}
static bool ht_isolator_action(ht_isolator_state *s){
 if(!s->active || s->paused)return false;
 if(!s->stage && s->tick==HT_ISOLATOR_WAIT){s->stage=1;s->tick=0;return true;}
 if(s->stage==2){s->active=false;return true;}
 return false;
}
static const char *ht_isolator_label(const ht_isolator_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->stage)return "I CLOSED IT WITHOUT RECONNECTING THE LINE.";
 if(s->tick<64)return "I WARMED THE ICE-STIFF CATCH IN MY HANDS.";
 if(s->tick<128)return "THE HOUSING OPENED.";
 if(s->tick<192)return "I TOOK THE KNIFE BY ITS DRY HANDLE.";
 if(s->tick<240)return "THE DRY KNIFE HANDLE LIFTED THE CONTACT.";
 if(s->tick<384)return "THE NEXT SET OF FLASHES DID NOT COME.";
 if(s->tick<528)return "I WAITED FOR ANOTHER INTERVAL. NOTHING.";
 return "A SILVER UNDERSIDE, JUST CLEAR OF ITS MATE.";
}
static ht_person_pose ht_isolator_pose(const ht_game *game,const ht_isolator_state *s){
 ht_game v=*game;int offset=17,walking=0;
 unsigned lid=ht_isolator_lid(s->stage,s->tick);int edge=13-(int)lid*37/64;
 if(!s->stage && s->tick<64)offset=20;
 else if(!s->stage && s->tick<128){offset=edge+7;walking=-1;}
 else if(!s->stage && s->tick<192){offset=-17+(int)(s->tick-128)*34/64;walking=1;}
 else if(s->stage==1 && s->tick<64){offset=17-(int)s->tick*34/64;walking=-1;}
 else if(s->stage){offset=edge+7;walking=s->stage==1?1:0;}
 int x=HT_ISOLATOR_X+offset;
 v.x=x*256;v.y=ht_surface_at(&v,9,x)*256;v.vx=walking*248;v.vy=0;v.facing=(s->stage==1 && s->tick>=64)?-1:walking?walking:-1;v.grounded=true;v.ticks=s->tick;v.stride=(unsigned)s->tick*128;
 int sx=x-v.camera/256;ht_person_pose p=ht_person_pose_at(sx,(v.y-v.camera_y)/256,&v);
 int f=ht_level_land(8)[9].top-v.camera_y/256,at=HT_ISOLATOR_X-v.camera/256;
 if(walking){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=sx+(p.foot[i].x-sx)*3/4;}
 if(!s->stage && s->tick>=192){p.hand[0]=ht_joint_at(at+9,f-25-(int)ht_isolator_lift(s->tick));p.elbow[0]=1;}
 else if(!walking || (!s->stage && s->tick<128) || (s->stage==1 && s->tick>=64)){p.hand[0]=ht_joint_at(at+edge-2,f-29);p.elbow[0]=1;p.hand[1]=ht_joint_at(at+edge-1,f-27);p.elbow[1]=1;}
 return p;
}
static void ht_isolator_study_render(const ht_game *game,const ht_isolator_state *s){
 ht_game v=*game;bool mast=!s->stage && s->tick>=264 && s->tick<528;
 v.intimacy=0;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.isolator_view_active=true;v.isolator_view_stage=s->stage;v.isolator_view_tick=s->tick;
 if(!s->stage && s->tick<240)v.puzzle.stage=0; /* Show the earned physical opening before the wait. */
 if(mast){
  int wx=ht_landmark_x(8,1),floor=ht_surface_at(&v,6,wx);
  v.camera=(wx-240)*256;v.camera_y=(floor-220)*256;v.x=wx*256;v.y=floor*256;v.ticks=game->ticks+s->tick-240;
 }else{v.camera=(HT_ISOLATOR_X-250)*256;v.camera_y=(ht_level_land(8)[9].top-190)*256;}
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);
 ht_render_scene_from(&v,false);
 if(!mast){
  ht_world_scale=ht_scene_scale(&v);ht_person_pose p=ht_isolator_pose(&v,s);ht_person_draw(&p);
  if(!s->stage && s->tick>=192){
   /* The dry handle touches the contact; the damaged blade points away. */
   int tip=HT_ISOLATOR_X+3-v.camera/256,y=ht_level_land(8)[9].top-v.camera_y/256-29-(int)ht_isolator_lift(s->tick);
   ht_limb(ht_scene,p.hand[0].x,p.hand[0].y,tip,y,1,1,160);
   ht_triangle(ht_scene,p.hand[0].x+1,p.hand[0].y+1,p.hand[0].x+7,p.hand[0].y+5,p.hand[0].x+5,p.hand[0].y+5,102);
  }
 }
 ht_world_scale=256;ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_isolator_label(s);
 ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=s->stage==2?"A RETURN TO CHIMES  START NOTE  X RETURN":!s->stage && s->tick==HT_ISOLATOR_WAIT?"A CLOSE HOUSING  START NOTE  X RETURN":"START NOTE  PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
