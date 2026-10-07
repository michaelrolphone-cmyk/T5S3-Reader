#define HT_FIRST_HOUSE_STUDY 1
typedef struct {bool active,paused;int8_t lean,direction;uint8_t stage;uint16_t tick;} ht_first_house_state;
static ht_first_house_state ht_first_house;
static bool ht_first_house_near(const ht_game *g){return g->level==9 && !g->door_stage && g->grounded && !g->traversal.mode && ht_abs(g->x/256-(HT_FIRST_HOUSE_X-18))<=16 && ht_abs(g->y/256-ht_surface_at(g,0,g->x/256))<3;}
static void ht_first_house_begin(void){ht_first_house=(ht_first_house_state){true,false,0,0,0,0};}
static bool ht_first_house_step(ht_first_house_state *s){
 if(!s->active || s->paused || s->stage==2)return false;
 if(!s->stage){int before=s->lean;s->lean=(int8_t)ht_clamp(before+s->direction,-16,16);return before!=s->lean;}
 if(++s->tick==448)s->stage=2;
 return true;
}
static bool ht_first_house_action(ht_first_house_state *s){
 if(!s->active || s->paused || s->stage==1)return false;
 if(s->stage==2){s->active=false;return true;}
 s->stage=1;s->tick=0;s->direction=0;return true;
}
static const char *ht_first_house_label(const ht_first_house_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return ht_abs(s->lean)<4?"A CLEAR CHANNEL SHOWED A VERTICAL EDGE.":"THE EDGE MOVED TOO. A REFLECTED DOORPOST.";
 if(s->tick<96)return "THE LOWER PANE WAS CLOUDED FROM WITHIN.";
 if(s->tick<160)return "I KNOCKED WITH MY BARE KNUCKLES.";
 if(s->tick<320)return "I WAITED, THEN WAITED LONGER.";
 return "NOTHING MOVED BEHIND THE GLASS.";
}
static ht_person_pose ht_first_house_pose(const ht_game *game,const ht_first_house_state *s){
 ht_game v=*game;int walk=s->stage?(int)ht_min(s->tick,96):0;
 int wx=HT_FIRST_HOUSE_X-18+44*walk/96;v.x=wx*256;v.y=ht_surface_at(&v,0,wx)*256;
 v.vx=s->stage && s->tick<96?248:0;v.vy=0;v.facing=1;v.grounded=true;v.ticks=s->tick;v.stride=(unsigned)walk*112;
 int x=wx-v.camera/256;ht_person_pose p=ht_person_pose_at(x,(v.y-v.camera_y)/256,&v);
 if(v.vx){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=x+(p.foot[i].x-x)*3/4;}
 if(!s->stage){p.shoulder.x+=s->lean/4;for(int i=0;i<2;++i)p.hand[i].x+=s->lean/4;}
 else if(s->tick>=80 && s->tick<160){
  int back=s->tick<96?0:s->tick<112?(int)(s->tick-96)*4/16:s->tick<128?4-(int)(s->tick-112)*4/16:0;
  int mix=s->tick<96?(int)(s->tick-80)*256/16:s->tick<136?256:(int)(160-s->tick)*256/24;
  int tx=HT_FIRST_HOUSE_X+34-v.camera/256-back,ty=ht_first_house_floor()-v.camera_y/256-26;
  p.hand[0].x+=(tx-p.hand[0].x)*mix/256;p.hand[0].y+=(ty-p.hand[0].y)*mix/256;p.elbow[0]=1;
 }
 return p;
}
static void ht_first_house_study_render(const ht_game *game,const ht_first_house_state *s){
 ht_game v=*game;v.camera=(HT_FIRST_HOUSE_X-210)*256;v.camera_y=(ht_first_house_floor()-190)*256;
 v.intimacy=128;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;v.first_house_lean=(int8_t)(s->lean+(s->stage?44*(int)ht_min(s->tick,96)/96:0));
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);
 ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);ht_person_pose p=ht_first_house_pose(&v,s);ht_person_draw(&p);
 ht_world_scale=256;ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_first_house_label(s);
 ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"L/R LEAN  A KNOCK  START JOURNAL  X RETURN":s->stage==2?"A RETURN  START JOURNAL  X RETURN":"START JOURNAL  PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_first_house_prompt(const ht_game *g){if(ht_first_house_near(g)){ht_rect(ht_scene,100,33,280,19,255);ht_text_color(108,39,"A / CONFIRM EXAMINE THE FIRST HOUSE",1,85);}}
