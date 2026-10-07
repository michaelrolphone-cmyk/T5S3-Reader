#define HT_LIVING_BED_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_bed_state;
static ht_bed_state ht_bed;
static bool ht_bed_near(const ht_game *g){return g->level==6 && g->grounded && !g->traversal.mode && ht_abs(g->x/256-(HT_BED_X-7))<=7 && ht_abs(g->y/256-ht_surface_at(g,0,g->x/256))<3;}
static void ht_bed_begin(void){ht_bed=(ht_bed_state){true,false,0,0};}
static bool ht_bed_step(ht_bed_state *s){
 if(!s->active || s->paused || (s->stage&1))return false;
 unsigned limit=!s->stage?128:s->stage==4?192:96;
 if(++s->tick==limit){if(s->stage==8)s->active=false;else{++s->stage;s->tick=0;}}return true;
}
static bool ht_bed_action(ht_bed_state *s){if(!s->active || s->paused || !(s->stage&1))return false;++s->stage;s->tick=0;return true;}
static const char *ht_bed_label(const ht_bed_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->stage<2)return "THE BEDS WERE ALIVE.";
 if(s->stage<4)return "QUARRY DUST LEFT A PALE CRESCENT ON THE LEAF.";
 if(s->stage==4)return "I WETTED MY FINGER AND LIFTED IT AWAY.";
 if(s->stage==5)return "THE LEAF BENT UNDER THE TOUCH, THEN ROSE.";
 if(s->stage==6)return "I LAID MY PALM ON THE SOIL.";
 return "WARM.";
}
static ht_joint ht_bed_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_bed_pose(const ht_game *game,const ht_bed_state *s){
 ht_game v=*game;int wx=HT_BED_X-7,f=ht_bed_floor(),x=wx-v.camera/256,y=f-v.camera_y/256;
 if(!s->stage)wx=game->x/256+(wx-game->x/256)*(int)ht_min(s->tick,32)/32;
 x=wx-v.camera/256;
 v.x=wx*256;v.y=f*256;v.vx=v.vy=0;v.grounded=true;v.landing_ticks=0;v.facing=1;v.stride=0;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(x,y,&v);
 unsigned kneel=!s->stage?(unsigned)ht_clamp((int)s->tick-32,0,96)*256/96:s->stage==8?256u-(unsigned)ht_min(s->tick,96)*256/96:256u;
 p.hip=ht_bed_mix(p.hip,ht_joint_at(x,y-7),kneel);p.shoulder=ht_bed_mix(p.shoulder,ht_joint_at(x+5,y-14),kneel);
 p.foot[0]=ht_bed_mix(p.foot[0],ht_joint_at(x-5,y),kneel);p.foot[1]=ht_bed_mix(p.foot[1],ht_joint_at(x+9,y),kneel);
 p.hand[0]=ht_bed_mix(p.hand[0],ht_joint_at(x+11,y-9),kneel);p.hand[1]=ht_bed_mix(p.hand[1],ht_joint_at(x-1,y-7),kneel);
 p.knee[0]=-1;p.knee[1]=1;
 unsigned bend=ht_bed_bend(s->stage,s->tick);ht_joint leaf=ht_joint_at(HT_BED_X+8-v.camera/256,y-15+(int)bend*5/256);
 if(s->stage==2)p.hand[0]=ht_bed_mix(p.hand[0],leaf,ht_min(s->tick,48)*256/48);
 else if(s->stage==3)p.hand[0]=leaf;
 else if(s->stage==4){
  ht_joint wet=ht_joint_at(p.shoulder.x+3,p.shoulder.y-3);
  if(s->tick<40)p.hand[0]=ht_bed_mix(leaf,wet,s->tick*256/40);
  else if(s->tick<64)p.hand[0]=wet;
  else if(s->tick<112)p.hand[0]=ht_bed_mix(wet,leaf,(s->tick-64)*256/48);
  else if(s->tick<128)p.hand[0]=leaf;
  else p.hand[0]=ht_bed_mix(leaf,p.hand[0],(s->tick-128)*256/64);
 }else if(s->stage==6 || s->stage==7 || s->stage==8){
  unsigned q=s->stage==7?256:s->stage==8?256-ht_min(s->tick,96)*256/96:ht_min(s->tick,64)*256/64;
  p.shoulder=ht_bed_mix(p.shoulder,ht_joint_at(x+8,y-10),q);
  p.hand[0]=ht_bed_mix(p.hand[0],ht_joint_at(x+12,y-1),q);
 }
 return p;
}
static void ht_bed_leaf_insert(const ht_bed_state *s){
 int bend=s->stage==3?26:0;
 /* The same leaf fills the held insert; its crescent and released lift are physical. */
 ht_triangle(ht_scene,85,190,349,184,405,223,189);ht_triangle(ht_scene,85,190,405,223,56,224,211);
 for(int n=0;n<58;++n){int xx=76+(int)(ht_hash(n+113)%300),yy=198+(int)(ht_hash(n+557)%23);ht_rect(ht_scene,xx,yy,2,1,157);}
 ht_limb(ht_scene,190,203,211,143,3,2,210);
 static const int16_t edge[][2]={{208,151},{220,123},{244,110},{273,105},{301,103},{325,100},{308,125},{292,143},{273,158},{244,163},{223,159},{208,151}};
 for(unsigned n=0;n+1<sizeof(edge)/sizeof(edge[0]);++n){int ax=edge[n][0],ay=edge[n][1]+bend*(ax-208)/117,bx=edge[n+1][0],by=edge[n+1][1]+bend*(bx-208)/117;ht_triangle(ht_scene,249,137+bend/3,ax,ay,bx,by,145);ht_limb(ht_scene,ax,ay,bx,by,1,1,184);}
 ht_limb(ht_scene,209,151,316,104+bend,1,1,215);
 for(int n=0;n<4;++n){int xx=226+n*18,yy=144-n*8+bend*(n+1)/5;ht_limb(ht_scene,xx,yy,xx+2,yy-17,1,0,173);ht_limb(ht_scene,xx,yy,xx+17,yy+7,1,0,173);}
 if(s->stage==3){
  ht_limb(ht_scene,255,138,263,147,4,3,41);ht_limb(ht_scene,263,147,274,146,3,2,41);
  ht_limb(ht_scene,257,136,264,141,2,2,145);ht_limb(ht_scene,264,141,273,141,2,2,145);
  ht_limb(ht_scene,161,225,206,183,7,5,238);ht_limb(ht_scene,206,183,252,149,5,2,237);
 }
}
static void ht_bed_study_render(const ht_game *game,const ht_bed_state *s){
 ht_game v=*game;v.camera=(HT_BED_X-228)*256;v.camera_y=(ht_bed_floor()-192)*256;v.intimacy=0;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.bed_view_stage=s->stage;v.bed_view_tick=s->tick;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);ht_person_pose p=ht_bed_pose(&v,s);if(s->stage!=3 && s->stage!=5)ht_person_draw(&p);
 ht_world_scale=256;if(s->stage==3 || s->stage==5)ht_bed_leaf_insert(s);
 ht_world_scale=256;ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_bed_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=s->stage==1?"A TOUCH LEAF  START NOTES  X RETURN":s->stage==3?"A CLEAN DUST  START NOTES  X RETURN":s->stage==5?"A TOUCH SOIL  START NOTES  X RETURN":s->stage==7?"A STAND  START NOTES  X RETURN":"PAUSE HOLD  START NOTES  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_bed_prompt(const ht_game *g){if(!ht_bed_near(g))return;ht_rect(ht_scene,134,221,212,15,255);ht_text_color(145,225,"A KNEEL BESIDE THE LIVING BED",1,85);}
