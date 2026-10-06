#define HT_CABIN_STUDY 1
typedef struct {bool active,paused;int8_t direction,tilt;uint8_t stage;uint16_t tick;} ht_cabin_state;
static ht_cabin_state ht_cabin;
static bool ht_cabin_near(const ht_game *g){return g->level==3 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==1;}
static void ht_cabin_begin(void){ht_cabin=(ht_cabin_state){true,false,0,0,0,0};}
static bool ht_cabin_step(ht_cabin_state *s){
 if(!s->active || s->paused)return false;
 if(s->stage==2){int old=s->tilt;s->tilt=(int8_t)ht_clamp(old+s->direction,-16,16);return old!=s->tilt;}
 unsigned limit=s->stage==1?HT_CABIN_PICKUP_TICKS:s->stage==3?HT_CABIN_UNROLL_START+HT_CABIN_UNROLL_TICKS:s->stage==5?128:0;
 if(!limit)return false;
 if(++s->tick==limit){++s->stage;s->tick=0;}return true;
}
static bool ht_cabin_action(ht_cabin_state *s){
 if(!s->active || s->paused || (s->stage!=0 && s->stage!=2 && s->stage!=4))return false;
 ++s->stage;s->tick=0;s->direction=0;return true;
}
static const char *ht_cabin_label(const ht_cabin_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "THE MAP HAD BEEN CORRECTED IN HER HAND.";
 if(s->stage==1)return "I TILTED IT TOWARD THE WINDOW.";
 if(s->stage==2)return s->tilt>=12?"WHO HAS BEEN HERE BEFORE?":"ONLY THE PRESSURE SURVIVED.";
 if(s->stage==3)return s->tick<HT_CABIN_UNROLL_START?"A TWIST OF PAPER HAD BEEN WRAPPED AS A GRIP.":"I UNROLLED IT, EXPECTING ANOTHER INSTRUCTION.";
 if(s->stage==4)return "THE LEGS WERE TOO LONG, AND THERE WERE FIVE.";
 if(s->stage==5)return "I PUT IT BACK AROUND THE LEVER AS CAREFULLY AS I COULD.";
 return "THE PAPER WAS SOFT FROM HAVING BEEN HELD.";
}
static int ht_cabin_map_y(int x,int y,int tilt){return y+(240-x)*tilt/200;}
static void ht_cabin_map_line(int ax,int ay,int bx,int by,int tilt,int ink){ht_limb(ht_scene,ax,ht_cabin_map_y(ax,ay,tilt),bx,ht_cabin_map_y(bx,by,tilt),1,1,ink);}
static void ht_cabin_map_text(int x,int y,const char *text,int tilt,int ink){
 for(;*text;++text,x+=6){const uint8_t *glyph=ht_glyph(*text);for(int a=0;a<5;++a)for(int b=0;b<7;++b)if(glyph[a]&(1u<<b))ht_rect(ht_scene,x+a,ht_cabin_map_y(x+a,y+b,tilt),1,1,ink);}
}
static void ht_cabin_map(int tilt){
 int ly=ht_cabin_map_y(136,65,tilt),ry=ht_cabin_map_y(344,65,tilt),lb=ht_cabin_map_y(136,196,tilt),rb=ht_cabin_map_y(344,196,tilt);
 ht_triangle(ht_scene,136,ly,344,ry,344,rb,55);ht_triangle(ht_scene,136,ly,344,rb,136,lb,55);
 ht_cabin_map_line(139,193,341,193,tilt,136);
 ht_cabin_map_text(157,77,"MAIN  LOOP  SPUR",tilt,179);
 for(int road=0;road<3;++road){int y=101+road*16,left=158+road*22;
  ht_cabin_map_line(left,y,321,y,tilt,178);ht_cabin_map_line(left,y+3,321,y+3,tilt,178);
  for(int n=0;n<6-road;++n)ht_cabin_map_line(left+n*23,y-2,left+n*23,y+6,tilt,124);
 }
 /* Erased spur and the replacement crossing remain separate strokes. */
 ht_cabin_map_line(214,117,248,91,tilt,77);ht_cabin_map_line(215,117,249,91,tilt,70);
 ht_cabin_map_line(209,117,262,137,tilt,214);ht_cabin_map_line(210,119,263,139,tilt,191);
 int reveal=ht_clamp((tilt-4)*16,0,192),ink=55+140*reveal/192;
 ht_cabin_map_text(190,155,"WHO HAS BEEN",tilt,ink);ht_cabin_map_text(190,167,"HERE BEFORE?",tilt,ink);
 /* Hands remain attached to the same lower paper corners as it tilts. */
 ht_limb(ht_scene,105,250,140,lb-10,5,4,242);ht_disk(ht_scene,140,lb-10,5,225);
 ht_limb(ht_scene,371,250,340,rb-10,5,4,242);ht_disk(ht_scene,340,rb-10,5,225);
}
static void ht_cabin_paper_stroke(int ax,int ay,int bx,int by,int left,int right,int ink){
 if(ht_max(ax,bx)<left || ht_min(ax,bx)>right)return;
 if(ax<left){ay+=(by-ay)*(left-ax)/(bx-ax);ax=left;}else if(ax>right){ay+=(by-ay)*(right-ax)/(bx-ax);ax=right;}
 if(bx<left){by+=(ay-by)*(left-bx)/(ax-bx);bx=left;}else if(bx>right){by+=(ay-by)*(right-bx)/(ax-bx);bx=right;}
 ht_limb(ht_scene,ax,ay,bx,by,1,1,ink);
}
static void ht_cabin_dog(int left,int right){
 /* Clip the drawing while the rolled edges still conceal it. */
 static const int16_t line[][4]={{188,116,202,110},{202,110,276,113},{276,113,280,102},{280,102,294,99},{294,99,306,110},{306,110,296,118},{296,118,280,116},{280,116,281,133},{281,133,190,132},{190,132,188,116},{188,116,172,98},{280,102,281,91},{281,91,288,100}};
 for(unsigned n=0;n<sizeof(line)/sizeof(line[0]);++n)ht_cabin_paper_stroke(line[n][0],line[n][1],line[n][2],line[n][3],left,right,216);
 for(int n=0;n<5;++n){int x=195+n*18;if(x>=left && x+7<=right){ht_limb(ht_scene,x,132,x+3,169+(n%2)*4,1,1,218);ht_limb(ht_scene,x+3,169+(n%2)*4,x+7,170+(n%2)*4,1,1,218);}}
 if(left<192 && right>282){ht_limb(ht_scene,191,132,281,133,1,1,228);ht_limb(ht_scene,192,130,280,131,1,1,226);}
 if(left<289 && right>299){ht_disk(ht_scene,294,105,4,235);ht_disk(ht_scene,294,105,1,28);}
}
static unsigned ht_cabin_opening(const ht_cabin_state *s){
 if(s->stage==3)return (unsigned)ht_clamp((int)s->tick-HT_CABIN_UNROLL_START,0,HT_CABIN_UNROLL_TICKS)*256/HT_CABIN_UNROLL_TICKS;
 if(s->stage==5)return 256-ht_min(s->tick,64)*256/64;
 return 256;
}
static void ht_cabin_paper(const ht_cabin_state *s){
 unsigned open=ht_cabin_opening(s);int half=6+(int)open*104/256,left=240-half,right=240+half;
 ht_triangle(ht_scene,left,73,right,69,right,195,56);ht_triangle(ht_scene,left,73,right,195,left,191,56);
 ht_rect(ht_scene,left,132,right-left,1,125); /* Existing print crosses the belly. */
 ht_cabin_dog(left+3,right-3);
 int curl=ht_max(2,10-(int)open*8/256);ht_rect(ht_scene,right-curl,72,curl,119,91);
 ht_limb(ht_scene,right-curl,73,right-curl,190,1,1,146);
 ht_limb(ht_scene,116,248,left+4,180,5,4,242);ht_disk(ht_scene,left+4,180,5,225);
 ht_limb(ht_scene,363,248,right-4,176,5,4,242);ht_disk(ht_scene,right-4,176,5,225);
}
static ht_person_pose ht_cabin_pose(const ht_game *game,const ht_cabin_state *s){
 ht_game v=*game;int x=game->x/256,target=ht_cabin_x()-15,q=0;
 if(s->stage==1){q=ht_min(s->tick,64);x+=(target-x)*q/64;v.facing=-1;}
 else if(s->stage==3){q=ht_min(s->tick,64);x=target+18*q/64;v.facing=1;}
 else if(s->stage>=5){x=ht_cabin_x()+3;v.facing=1;q=64;}
 v.x=x*256;v.y=ht_surface_at(&v,4,x)*256;v.vx=(s->stage==1 || s->stage==3) && q<64?248:0;v.vy=0;v.grounded=true;v.landing_ticks=0;memset(&v.traversal,0,sizeof(v.traversal));v.stride=s->tick*96;
 ht_person_pose p=ht_person_pose_at(x-v.camera/256,(v.y-v.camera_y)/256,&v);if(v.vx){++p.hip.y;++p.shoulder.y;}
 if(s->stage==1 || s->stage==3 || s->stage==5){
  int reach=ht_clamp((q-48)*16,0,256);if(s->stage==5)reach=s->tick<112?256:ht_clamp((128-(int)s->tick)*16,0,256);
  int hx=ht_cabin_x()+(s->stage==1?-24:12)-v.camera/256,hy=ht_cabin_floor()-(s->stage==1?29:27)-v.camera_y/256;
  p.hand[0].x+=(hx-p.hand[0].x)*reach/256;p.hand[0].y+=(hy-p.hand[0].y)*reach/256;
 }
 return p;
}
static void ht_cabin_study_render(const ht_game *game,const ht_cabin_state *s){
 ht_game v=*game;v.camera=(ht_cabin_x()-236)*256;v.camera_y=(ht_cabin_floor()-192)*256;v.intimacy=0;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.cabin_view_stage=s->stage;v.cabin_view_tick=s->tick;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 if(s->stage==2){ht_world_scale=256;ht_cabin_map(s->tilt);}
 else if(s->stage==4 || (s->stage==3 && s->tick>=HT_CABIN_UNROLL_START) || (s->stage==5 && s->tick<64)){ht_world_scale=256;ht_cabin_paper(s);}
 else{ht_person_pose p=ht_cabin_pose(&v,s);ht_person_draw(&p);if(s->stage==5 && s->tick>=64 && s->tick<112)ht_rect(ht_scene,p.hand[0].x-2,p.hand[0].y-2,4,5,55);}
 ht_world_scale=256;ht_rect(ht_scene,7,226,466,35,255);const char *label=ht_cabin_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A TAKE MAP  START NOTE  X RETURN":s->stage==2?"L/R TILT MAP  A UNWRAP GRIP  X RETURN":s->stage==4?"A WRAP IT BACK  START NOTE  X RETURN":s->stage==6?"A READ NOTE  START NOTE  X RETURN":"START NOTE  PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
