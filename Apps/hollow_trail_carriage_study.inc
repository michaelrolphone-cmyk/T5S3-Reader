#define HT_CARRIAGE_STUDY 1
#define HT_CARRIAGE_DREAM 1
#define HT_CARRIAGE_REFLECTION_TICKS 352u
#define HT_CARRIAGE_DREAM_TICKS 704u
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_carriage_state;
static ht_carriage_state ht_carriage;
#include "hollow_trail_carriage_dream.inc"
static bool ht_carriage_near(const ht_game *g){return g->level==3 && g->grounded && !g->traversal.mode && ht_abs(g->x/256-(HT_CARRIAGE_X-16))<=6;}
static void ht_carriage_begin(void){ht_carriage=(ht_carriage_state){true,false,0,0};}
static bool ht_carriage_step(ht_carriage_state *s){
 if(!s->active || s->paused || s->stage==1 || s->stage==3 || s->stage==5 || s->stage==9)return false;
 unsigned limit=s->stage==0?184:s->stage==2?224:s->stage==4?288:s->stage==7?HT_CARRIAGE_REFLECTION_TICKS:s->stage==8?HT_CARRIAGE_DREAM_TICKS:344;
 if(++s->tick==limit){
  if(s->stage==6)s->active=false;
  else {s->stage=s->stage==2?7:s->stage==7?3:s->stage==8?9:s->stage+1;s->tick=0;}
 }
 return true;
}
static bool ht_carriage_action(ht_carriage_state *s){
 if(!s->active || s->paused || (s->stage!=1 && s->stage!=3 && s->stage!=5 && s->stage!=9))return false;
 s->stage=s->stage==3?8:s->stage==9?4:s->stage+1;s->tick=0;return true;
}
static ht_joint ht_carriage_mix(ht_joint a,ht_joint b,int q,int span){return ht_joint_at(a.x+(b.x-a.x)*q/span,a.y+(b.y-a.y)*q/span);}
static ht_person_pose ht_carriage_entry_pose(const ht_game *g,unsigned tick){
 /* Foot lifts precede crossing either jamb. One planted contact remains on
  * ground, timber, sill or cabin floor throughout; reverse uses the same path. */
 static const uint16_t time[]={0,24,56,68,80,92,104,116,128,144,160};
 static const int16_t joint[][12]={
  {250,-14,251,-22,248,0,252,0,249,-12,252,-12},
  {250,-14,251,-22,248,0,252,0,260,-26,261,-26},
  {253,-15,254,-24,248,0,258,-8,260,-26,261,-26},
  {257,-20,258,-28,258,-8,258,-26,260,-32,264,-26},
  {258,-22,259,-30,258,-8,263,-26,260,-32,264,-26},
  {263,-38,265,-44,258,-26,263,-26,260,-40,276,-44},
  {265,-40,266,-48,267,-26,263,-26,260,-40,276,-44},
  {272,-39,273,-47,267,-26,278,-26,271,-37,276,-44},
  {276,-25,277,-33,267,-26,284,-12,276,-24,276,-44},
  {282,-25,283,-33,278,-26,284,-12,281,-23,284,-23},
  {288,-26,289,-34,286,-12,290,-12,287,-24,290,-24}
 };
 ht_game actor=*g;actor.vx=actor.vy=0;actor.facing=1;actor.landing_ticks=0;memset(&actor.traversal,0,sizeof(actor.traversal));
 int f=ht_carriage_ground(),cx=g->camera/256-HT_CARRIAGE_SHIFT,cy=g->camera_y/256;
 if(tick<24){
  int from=g->x/256-HT_CARRIAGE_SHIFT;int x=from+(250-from)*(int)tick/24;actor.x=(x+HT_CARRIAGE_SHIFT)*256;actor.y=ht_surface_at(&actor,0,x+HT_CARRIAGE_SHIFT)*256;
  actor.vx=x==from?0:248;actor.stride=tick*96;
  ht_person_pose p=ht_person_pose_at(x-cx,(actor.y-g->camera_y)/256,&actor);if(actor.vx){++p.hip.y;++p.shoulder.y;}return p;
 }
 tick=ht_min(tick-24,160);unsigned k=0;while(k<9 && tick>time[k+1])++k;
 int q=(int)tick-time[k],span=time[k+1]-time[k];ht_joint j[6];
 for(int n=0;n<6;++n){
  ht_joint a=ht_joint_at(joint[k][n*2],f+joint[k][n*2+1]);
  ht_joint b=ht_joint_at(joint[k+1][n*2],f+joint[k+1][n*2+1]);
  j[n]=ht_carriage_mix(a,b,q,span);
  if((n==2 || n==3) && a.x!=b.x && q && q<span)j[n].y-=ht_max(1,(a.y==b.y?4:8)*4*q*(span-q)/(span*span));
  j[n].x-=cx;j[n].y-=cy;
 }
 ht_person_pose p=ht_person_pose_at(288-cx,f-12-cy,&actor);
 p.hip=j[0];p.shoulder=j[1];p.foot[0]=j[2];p.foot[1]=j[3];p.hand[0]=j[4];p.hand[1]=j[5];return p;
}
static ht_person_pose ht_carriage_rest_pose(const ht_game *g,unsigned tick,bool waking){
 int f=ht_carriage_ground()-g->camera_y/256,cx=g->camera/256-HT_CARRIAGE_SHIFT;
 ht_game actor=*g;actor.vx=actor.vy=0;actor.facing=1;actor.landing_ticks=0;memset(&actor.traversal,0,sizeof(actor.traversal));
 if(tick<48){int x=288+(int)tick*22/48;actor.x=(x+HT_CARRIAGE_SHIFT)*256;actor.vx=248;actor.stride=tick*96;
  ht_person_pose p=ht_person_pose_at(x-cx,f-12,&actor);++p.hip.y;++p.shoulder.y;return p;}
 ht_person_pose p=ht_person_pose_at(310-cx,f-12,&actor);
 if(tick<112){
  int bend=tick<80?((int)tick-48)*256/32:(112-(int)tick)*256/32;
  p.hip=ht_joint_at(310-cx,f-25);p.shoulder=ht_joint_at(310-cx,f-34+12*bend/256);
  p.hand[0]=ht_joint_at(308-cx,p.shoulder.y+9);p.hand[1]=ht_joint_at(312-cx,p.shoulder.y+9);return p;
 }
 int q=ht_clamp((int)tick-112,0,64);
 ht_joint start[6]={p.hip,p.shoulder,p.foot[0],p.foot[1],p.hand[0],p.hand[1]};
 ht_joint end[6]={ht_joint_at(318-cx,f-30),ht_joint_at(306-cx,f-33),ht_joint_at(330-cx,f-26),ht_joint_at(328-cx,f-24),ht_joint_at(310-cx,f-26),ht_joint_at(307-cx,f-24)};
 ht_joint j[6];for(int i=0;i<6;++i)j[i]=ht_carriage_mix(start[i],end[i],q,64);
 p.hip=j[0];p.shoulder=j[1];p.foot[0]=j[2];p.foot[1]=j[3];p.hand[0]=j[4];p.hand[1]=j[5];p.facing=-1;
 if(waking){p.hand[0]=ht_joint_at(296-cx,f-31);p.elbow[0]=-1;}
 return p;
}
static unsigned ht_carriage_pan(const ht_carriage_state *s){
 if(s->stage==5)return 256;
 if(s->stage==4)return (unsigned)ht_clamp((int)s->tick-128,0,160)*256/160;
 if(s->stage==6)return 256-ht_min(s->tick,160)*256/160;
 return 0;
}
static const char *ht_carriage_label(const ht_carriage_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "A BROKEN WINDOW GAVE ME ROOM TO CLIMB INSIDE.";
 if(s->stage==1)return "I CHOSE THE DRY BENCH.";
 if(s->stage==2)return s->tick<112?"I TOOK OFF MY BOOTS.":"I PUT MY FEET UNDER MY COAT.";
 if(s->stage==3)return "INSTEAD I HEARD NOTHING.";
 if(s->stage==7)return s->tick<176?"IN THE PANE, MY REFLECTION LAY SIDEWAYS.":"I TURNED TOWARD THE SEAT BACK.";
 if(s->stage==8){
  if(s->tick<176)return "NEAR DAWN I DREAMED MY SISTER SAT OPPOSITE ME.";
  if(s->tick<352)return "EACH PAPER TURNED OUT TO BE A HOUSEHOLD ACCOUNT.";
  if(s->tick<528)return "SHE SAID I HAD LEFT SOMETHING OUT.";
  return "\"WHAT?\"  SHE POINTED TO A BLANK PLACE.";
 }
 if(s->stage==9)return "\"YOU'LL REMEMBER.\"";
 if(s->stage==4 && s->tick<128)return "I WOKE WITH MY HAND EXTENDED ACROSS THE CARRIAGE.";
 if(s->stage==4 || s->stage==5)return "MORNING SHOWED ME THE LOCOMOTIVE.";
 return "THE RAILS REMAINED SILENT.";
}
static void ht_carriage_study_render(const ht_game *game,const ht_carriage_state *s){
 ht_game v=*game;unsigned pan=ht_carriage_pan(s);v.camera=(HT_CARRIAGE_X-180+(770-HT_CARRIAGE_X)*(int)pan/256)*256;
 v.camera_y=(ht_carriage_ground()-200)*256;v.vx=v.vy=0;v.intimacy=0;v.vista=pan*192/256;
 v.drop_zoom=v.rotation_phase=v.sway_phase=0;v.carriage_view_active=true;
 v.carriage_view_boots=s->stage>=3 || (s->stage==2 && s->tick>=96);
 if(s->stage==6 && s->tick>=128)v.carriage_view_boots=false;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);
 ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 ht_person_pose p;
 if(!s->stage)p=ht_carriage_entry_pose(&v,s->tick);
 else if(s->stage==1)p=ht_carriage_entry_pose(&v,184);
 else if(s->stage==2)p=ht_carriage_rest_pose(&v,s->tick,false);
 else if(s->stage==6){
  if(s->tick<160)p=ht_carriage_rest_pose(&v,224-ht_min(s->tick,160)*224/160,false);
  else p=ht_carriage_entry_pose(&v,184-(s->tick-160));
 }else p=ht_carriage_rest_pose(&v,224,s->stage==4 && s->tick<128);
 ht_person_draw(&p);
 if((s->stage==2 && s->tick>=160) || s->stage==3 || s->stage==4 || s->stage==5 || (s->stage==6 && s->tick<48)){
  int x=HT_CARRIAGE_X-v.camera/256,f=ht_carriage_ground()-v.camera_y/256;
  ht_triangle(ht_scene,x+50,f-33,x+75,f-25,x+45,f-23,171);
 }
 ht_carriage_window_front(&v);ht_world_scale=256;
 /* Quiet night belongs to this chosen rest, never the live chapter clock. */
 int night=s->stage==3?90:s->stage==2?ht_clamp((int)s->tick-144,0,80)*90/80:s->stage==4?90-ht_min(s->tick,128)*90/128:0;
 if(night){int width=ht_native_active?HT_NATIVE_W:HT_W,height=ht_native_active?HT_NATIVE_H:HT_H;
  for(int y=0;y<height;++y){for(int x=0;x<width;++x){int at=y*width+x;ht_scene[at]=(uint8_t)(ht_scene[at]+(255-ht_scene[at])*(int)night/128);}if(!(y%32))ht_checkpoint();}}
 if(s->stage==7 || s->stage==3)ht_carriage_reflection_draw(s->stage==3?HT_CARRIAGE_REFLECTION_TICKS:s->tick);
 if(s->stage==8 || s->stage==9)ht_carriage_dream_draw(s->stage==9?HT_CARRIAGE_DREAM_TICKS:s->tick);
 ht_rect(ht_scene,7,226,466,35,255);const char *label=ht_carriage_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=s->stage==1?"A REST  START JOURNAL  X RETURN":s->stage==3?"A SLEEP  START JOURNAL  X RETURN":s->stage==9?"A WAKE  START JOURNAL  X RETURN":s->stage==5?"A LEAVE CARRIAGE  START JOURNAL  X RETURN":"START JOURNAL  PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_carriage_prompt(const ht_game *g){
 if(!ht_carriage_near(g))return;
 ht_rect(ht_scene,128,221,224,15,255);ht_text_color(139,225,"A ENTER THE WHEEL-LESS CARRIAGE",1,85);
}
