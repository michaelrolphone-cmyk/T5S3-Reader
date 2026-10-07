/* Earned quarry-to-gallery handoff. The live game has already taken its
 * original spawn; this bounded view never edits that destination or puzzle. */
#define HT_HOIST_TICKS 640u
typedef struct {bool active,paused;uint16_t tick;ht_game source,destination;} ht_hoist_state;
static ht_hoist_state ht_hoist;
static bool ht_hoist_earned(const ht_game *before,const ht_game *after){
 return before->level==5 && after->level==6 && before->grounded &&
        before->x>=(HT_GOAL-8)*256 && before->puzzle.solved && before->puzzle.opening==48;
}
static void ht_hoist_begin(const ht_game *before,const ht_game *after){
 ht_hoist=(ht_hoist_state){0};ht_hoist.active=true;ht_hoist.source=*before;ht_hoist.destination=*after;
}
static bool ht_hoist_step(ht_hoist_state *s){
 if(!s->active || s->paused || s->tick>=HT_HOIST_TICKS)return false;
 ++s->tick;return true;
}
static int ht_hoist_descent(unsigned tick){return ht_clamp((int)tick-128,0,384)*240/384;}
static int ht_hoist_floor(const ht_hoist_state *s,int x){
 int tilt=s->tick>=32 && s->tick<96?1:0;
 return 188+(x-240)*tilt/12;
}
static ht_joint ht_hoist_grip(const ht_hoist_state *s,int hand){
 int pull=ht_clamp((int)s->tick-96,0,32)*3/32;
 return ht_joint_at(hand?248:249,hand?163+pull:159+pull);
}
static ht_person_pose ht_hoist_pose(const ht_hoist_state *s){
 ht_game v=s->destination;v.traversal=(ht_traversal){0};v.vx=v.vy=0;v.grounded=true;v.facing=1;v.landing_ticks=0;
 if(s->tick>=544){
  int x=238+ht_clamp((int)s->tick-544,0,64)*62/64;
  v.camera=(s->destination.x/256-300)*256;v.camera_y=s->destination.y-188*256;
  v.x=(x+v.camera/256)*256;v.y=ht_surface_at(&v,0,v.x/256)*256;
  v.vx=s->tick<608?248:0;v.stride=s->tick*96;
  ht_person_pose p=ht_person_pose_at(x,(v.y-v.camera_y)/256,&v);
  if(s->tick<608){++p.hip.y;++p.shoulder.y;}
  return p;
 }
 ht_person_pose p=ht_person_pose_at(240,188,&v);
 int shift=ht_clamp((int)s->tick-32,0,32);
 p.hip=ht_joint_at(240-2*shift/32,175);p.shoulder=ht_joint_at(p.hip.x+2,166);
 p.foot[0]=ht_joint_at(238-8*shift/32,0);p.foot[1]=ht_joint_at(242+2*shift/32,0);
 for(int i=0;i<2;++i){
  p.foot[i].y=ht_hoist_floor(s,p.foot[i].x);
  ht_joint hand=ht_hoist_grip(s,i);int reach=ht_min(ht_clamp((int)s->tick-64,0,32),ht_clamp(544-(int)s->tick,0,32));
  p.hand[i].x+=(hand.x-p.hand[i].x)*reach/32;p.hand[i].y+=(hand.y-p.hand[i].y)*reach/32;
 }
 return p;
}
static const char *ht_hoist_label(const ht_hoist_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->tick<96)return "DARKNESS SHOWED BETWEEN MY BOOTS.";
 if(s->tick<160)return "I TOOK THE BRAKE CORD IN BOTH HANDS.";
 if(s->tick<300)return "THE QUARRY WALL SLID PAST.";
 if(s->tick<440)return "THE NEWEST FROND WAS STILL CURLED.";
 if(s->tick<512)return "THE CAGE REACHED THE LOWER GALLERY.";
 return "SEVEN, INCLUDING YOU.";
}
static void ht_hoist_render(const ht_hoist_state *s){
 ht_native_active=ht_camera_mode==HT_CAMERA_NATIVE && ht_native_a;ht_native_foreground_half_y=false;
 ht_scene=ht_native_active?ht_native_a:ht_scene_low;ht_world_scale=256;
 memset(ht_scene,166,ht_native_active?HT_NATIVE_PIXELS:HT_PIXELS);
 int descent=ht_hoist_descent(s->tick);
 /* The pale wall and damp rooted seams pass the stationary cage. */
 ht_rect(ht_scene,0,0,222,226,62);
 for(int i=0;i<12;++i){
  int y=20+i*38-descent;
  ht_limb(ht_scene,0,y,220,y+11,1,1,157);
  ht_limb(ht_scene,126,y+7,145,y+19,1,1,149);
  ht_limb(ht_scene,145,y+19,140,y+30,1,1,149);
  if(!(i%3))ht_rect(ht_scene,179,y+10,3,20,130);
  for(int j=0;j<11;++j){int x=(i*47+j*29)%213;ht_rect(ht_scene,x,y+16+(j*7)%19,2,1,83+(j%3)*9);}
  ht_checkpoint();
 }
 int fern=286-descent;
 ht_limb(ht_scene,208,fern+9,219,fern-9,1,1,186);
 for(int i=0;i<4;++i){int y=fern+5-i*4;ht_limb(ht_scene,211+i*2,y,200+i*3,y-5,1,1,183);ht_limb(ht_scene,211+i*2,y,222+i*2,y-2,1,1,183);}
 ht_limb(ht_scene,217,fern-5,221,fern-16,1,1,190);ht_limb(ht_scene,221,fern-16,225,fern-15,1,1,190);ht_rect(ht_scene,224,fern-15,2,4,190);
 /* Existing solved balance remains on the upper landing, with its real values. */
 ht_game upper=s->source;upper.camera=(HT_GOAL-240)*256;
 upper.camera_y=(ht_level_land(5)[9].top-188+descent)*256;
 ht_draw_mechanisms(&upper,ht_level_land(5)[9].top-upper.camera_y/256);
 ht_rect(ht_scene,0,188-descent,214,7,137);
 for(int x=264;x<480;++x){
  int world=s->destination.x/256+x-300;
  int y=s->tick<512?428-descent:ht_surface_at(&s->destination,0,world)-s->destination.y/256+188;
  ht_rect(ht_scene,x,y,1,8,136);
  if(x>=309 && x<423)ht_rect(ht_scene,x,y+1,1,2,77);
  if(x%17<6)ht_rect(ht_scene,x,y+4,1,1,187);
 }
 /* Pulley/cable, cage frame and individual floorboards share the pose floor. */
 ht_limb(ht_scene,238,94-descent,238,118,1,1,90);
 ht_limb(ht_scene,214,86-descent,238,94-descent,2,2,104);
 ht_disk(ht_scene,238,94-descent,7,113);ht_disk(ht_scene,238,94-descent,3,190);
 ht_rect(ht_scene,214,118,51,3,83);ht_rect(ht_scene,213,118,3,73,95);ht_rect(ht_scene,263,118,3,73,95);
 for(int x=216;x<264;++x){
  int floor=ht_hoist_floor(s,x);
  if((x>=233 && x<235) || (x>=248 && x<250)){ht_rect(ht_scene,x,floor,1,5,240);continue;}
  ht_rect(ht_scene,x,floor,1,5,x>=235 && x<248?105:137);
  if(x>=235 && x<248 && !(x%3))ht_rect(ht_scene,x,floor+1,1,2,106);
 }
 ht_joint grip=ht_hoist_grip(s,0),other=ht_hoist_grip(s,1);
 ht_limb(ht_scene,254,121,grip.x,grip.y,1,1,75);
 ht_limb(ht_scene,grip.x,grip.y,other.x,other.y,1,1,75);
 ht_limb(ht_scene,other.x,other.y,252,180,1,1,75);
 ht_person_pose p=ht_hoist_pose(s);ht_person_draw(&p);
 ht_rect(ht_scene,7,226,466,35,255);const char *label=ht_hoist_label(s);
 ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=s->tick==HT_HOIST_TICKS?"A CONTINUE  START JOURNAL  X CONTINUE":"START JOURNAL  PAUSE HOLD  X CONTINUE";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
