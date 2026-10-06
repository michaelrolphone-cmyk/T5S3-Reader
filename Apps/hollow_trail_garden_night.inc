#define HT_GARDEN_NIGHT 1
#define HT_NIGHT_X (HT_SLEEP_X+38+6*29)
typedef struct {bool active,paused;uint8_t stage;uint16_t tick,ambient,memory_tick;} ht_night_state;
static ht_night_state ht_night;
#include "hollow_trail_garden_memory.inc"
static int ht_night_floor(void){return ht_land_height(6,1,HT_NIGHT_X);}
static bool ht_night_near(const ht_game *g){return g->level==6 && g->grounded && !g->traversal.mode && ht_abs(g->x/256-(HT_NIGHT_X+12))<=7 && ht_abs(g->y/256-ht_surface_at(g,1,g->x/256))<=2;}
static void ht_night_begin(void){ht_night=(ht_night_state){true,false,0,0,0,0};}
static bool ht_night_step(ht_night_state *s){
 if(!s->active || s->paused)return false;
 if(s->stage==2){
  bool finishing=s->memory_tick==HT_SISTER_MEMORY_END-1;
  s->ambient=(uint16_t)((s->ambient+1)%384);
  if(s->memory_tick<HT_SISTER_MEMORY_END)++s->memory_tick;
  return ht_sister_memory_active(s) || finishing || !(s->ambient%8);
 }
 if(!(s->stage&1))return false;
 unsigned limit=s->stage==3?128:160;
 if(++s->tick==limit){++s->stage;s->tick=0;if(s->stage==6)s->active=false;}return true;
}
static bool ht_night_action(ht_night_state *s){if(!s->active || s->paused || (s->stage&1) || s->stage>=6)return false;++s->stage;s->tick=0;return true;}
static const char *ht_night_label(const ht_night_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "I CHOSE THE PALLET FARTHEST FROM THE DOOR.";
 if(s->stage==1)return "THE STRAW CRACKLED BENEATH ME.";
 if(ht_sister_memory_active(s))return ht_sister_memory_label(s);
 if(s->stage==2 && s->memory_tick==HT_SISTER_MEMORY_END)return "I TURNED TOWARD THE PIPE AND SLEPT.";
 if(s->stage==2)return "A LEAF TURNED IN THE RISING WARMTH.";
 if(s->stage==3 || s->stage==4)return "IN THE MORNING, LIGHT LAY ACROSS THE BEDS.";
 return "LIGHT LAY ACROSS THE BEDS IN LONG PALE BARS.";
}
static ht_person_pose ht_night_pose(const ht_game *g,const ht_night_state *s){
 ht_game v=*g;v.vx=v.vy=0;v.facing=-1;v.grounded=true;v.landing_ticks=0;v.stride=0;memset(&v.traversal,0,sizeof(v.traversal));
 int cx=v.camera/256,cy=v.camera_y/256,f=ht_night_floor()-cy,x=HT_NIGHT_X-cx;
 ht_person_pose p=ht_person_pose_at(g->x/256-cx,g->y/256-cy,&v);
 ht_joint start[6]={p.hip,p.shoulder,p.foot[0],p.foot[1],p.hand[0],p.hand[1]};
 ht_joint sit[6]={ht_joint_at(x+18,f-11),ht_joint_at(x+16,f-22),ht_joint_at(x+27,f),ht_joint_at(x+24,f),ht_joint_at(x+22,f-15),ht_joint_at(x+14,f-14)};
 ht_joint rest[6]={ht_joint_at(x+19,f-12),ht_joint_at(x+8,f-13),ht_joint_at(x+30,f-9),ht_joint_at(x+28,f-9),ht_joint_at(x+6,f-16),ht_joint_at(x+13,f-10)};
 unsigned t=s->stage==1?s->tick:s->stage==5?160u-ht_min(s->tick,160):s->stage>=2?160u:0;
 ht_joint j[6];for(unsigned i=0;i<6;++i)j[i]=t<80?ht_bed_mix(start[i],sit[i],t*256/80):ht_bed_mix(sit[i],rest[i],(t-80)*256/80);
 p.hip=j[0];p.shoulder=j[1];p.foot[0]=j[2];p.foot[1]=j[3];p.hand[0]=j[4];p.hand[1]=j[5];p.facing=-1;p.knee[0]=-1;p.knee[1]=1;return p;
}
static unsigned ht_night_leaf_width(unsigned phase){int t=(int)(phase%128);return (unsigned)ht_abs(t-64)*5/64;}
static void ht_night_details(const ht_game *g,const ht_night_state *s){
 int x=HT_NIGHT_X-g->camera/256,f=ht_night_floor()-g->camera_y/256;
 /* A folded end of sacking supports the head above the raised straw. */
 ht_triangle(ht_scene,x+2,f-12,x+9,f-16,x+15,f-12,94);
 ht_limb(ht_scene,x-8,f-2,x+47,f-2,2,2,153);ht_limb(ht_scene,x+47,f-2,x+50,f+3,2,2,153);
 for(int n=0;n<3;++n)ht_limb(ht_scene,x+7+n*18,f-4,x+7+n*18,f,1,0,190);
 /* The leaf changes visible width around one fixed centre; it does not drift. */
 int width=(int)ht_night_leaf_width(s->ambient);
 ht_limb(ht_scene,x+33,f-1,x+35,f-21,1,0,182);
 static const int8_t edge[][2]={{0,0},{-4,-2},{-5,-5},{-3,-8},{0,-12},{3,-8},{5,-5},{4,-2},{0,0}};
 for(unsigned i=0;i+1<sizeof(edge)/sizeof(edge[0]);++i)ht_triangle(ht_scene,x+35,f-27,x+35+edge[i][0]*width/5,f-21+edge[i][1],x+35+edge[i+1][0]*width/5,f-21+edge[i+1][1],i<4?117:143);
 ht_limb(ht_scene,x+35,f-21,x+35,f-32,0,0,166);
 if(s->stage==2){
  int water=(int)((s->ambient*7+s->ambient/19)%44);ht_rect(ht_scene,x-6+water,f-3,3,1,110);
  if((s->ambient%192)>=176)ht_rect(ht_scene,x-12,f-56+(int)(s->ambient%8)/4,1,2,102);
 }
 if(s->stage==3 || s->stage==4 || s->stage==5){
  int light=s->stage==3?ht_min(s->tick,128)*256/128:256;
  int width=ht_max(1,light*7/256);
  if(light)for(int n=0;n<3;++n){int xx=x-10+n*19;ht_triangle(ht_scene,xx-18,f-62,xx-18+width,f-62,xx+18,f-8,55);ht_triangle(ht_scene,xx-18+width,f-62,xx+18,f-8,xx+18+width,f-8,55);}
 }
}
static void ht_night_study_render(const ht_game *game,const ht_night_state *s){
 ht_game v=*game;v.camera=(HT_NIGHT_X+14-240)*256;v.camera_y=(ht_night_floor()-164)*256;v.intimacy=512;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 /* Share the caption-safe camera with the world before drawing bed contacts. */
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 ht_night_details(&v,s);ht_person_pose p=ht_night_pose(&v,s);ht_person_draw(&p);ht_world_scale=256;
 unsigned dark=s->stage==1?(unsigned)ht_clamp((int)s->tick-80,0,80)*100/80:s->stage==2?100u:s->stage==3?100u-(unsigned)ht_min(s->tick,128)*100/128:0;
 if(dark){int w=ht_native_active?HT_NATIVE_W:HT_W,h=ht_native_active?HT_NATIVE_H:HT_H;
  /* Dim only this chosen rest, never the chapter clock or live world state. */
  for(int y=0;y<h;++y){for(int x=0;x<w;++x){unsigned at=(unsigned)y*w+x;ht_scene[at]=(uint8_t)(ht_scene[at]+(255-ht_scene[at])*dark/128);}if(!(y%32))ht_checkpoint();}
 }
 if(ht_sister_memory_active(s))ht_sister_memory_draw(s);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_night_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A LIE DOWN  START NOTES  X RETURN":s->stage==2?"A WAKE  START NOTES  X RETURN":s->stage==4?"A STAND  START NOTES  X RETURN":"PAUSE HOLD  START NOTES  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_night_prompt(const ht_game *g){if(!ht_night_near(g))return;ht_rect(ht_scene,119,221,246,15,255);ht_text_color(129,225,"A REST ON THE FARTHEST PALLET",1,85);}
