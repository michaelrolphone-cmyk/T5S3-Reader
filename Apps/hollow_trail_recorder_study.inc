/* Chapter VIII: a manual, never an operating recorder or a new verdict.
 * Discovery and storm cessation still belong to ht_inspect's existing bit. */
#define HT_RECORDER_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_recorder_state;
static ht_recorder_state ht_recorder;
static bool ht_recorder_near(const ht_game *g){return g->level==7 && !g->traversal.mode && ht_evidence_near(g)==2;}
static void ht_recorder_begin(void){ht_recorder=(ht_recorder_state){true,false,0,0};}
static unsigned ht_recorder_limit(unsigned stage){return stage==1 || stage==3?96:stage==5?64:stage==7?160:0;}
static bool ht_recorder_step(ht_recorder_state *s){
 if(!s->active || s->paused || !ht_recorder_limit(s->stage))return false;
 if(++s->tick==ht_recorder_limit(s->stage)){++s->stage;s->tick=0;}return true;
}
static bool ht_recorder_action(ht_recorder_state *s){
 if(!s->active || s->paused || (s->stage&1) || s->stage>=8)return false;
 ++s->stage;s->tick=0;return true;
}
static const char *ht_recorder_label(const ht_recorder_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "A METAL CABINET IN A DRY RECESS.";
 if(s->stage==1)return "THE PAGES WERE PROTECTED BY A RUBBER SEAL.";
 if(s->stage==2)return "DRY PAGES. HANDLED CORNERS.";
 if(s->stage==3)return "THE DIAGRAM FROM THE GARDENS APPEARED AGAIN.";
 if(s->stage==4)return "A SIGNATURE PROVES A PATTERN, NOT A PERSON.";
 if(s->stage==5)return "HER MESSAGE LAY ON THE NEXT PAGE.";
 if(s->stage==6)return "TWO SHUTTERS SHARED ONE ARM.";
 if(s->stage==7)return "I TRACED THE LINKAGE WITH MY DAMAGED KNIFE.";
 return "A CLEAN THUMBPRINT MARKED THE DUSTY MARGIN.";
}
static ht_joint ht_recorder_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_recorder_pose(const ht_game *game,const ht_recorder_state *s){
 ht_game v=*game;unsigned q=s->stage==1?ht_min(s->tick,40)*256/40:s->stage>=2?256:0;
 int wx=game->x/256+(HT_RECORDER_X-3-game->x/256)*(int)q/256;
 v.x=wx*256;v.y=ht_recorder_floor()*256;v.vx=s->stage==1 && s->tick<40?248:0;v.vy=0;
 v.grounded=true;v.facing=1;v.landing_ticks=0;v.stride=s->tick*64;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(wx-v.camera/256,v.y/256-v.camera_y/256,&v);
 if(v.vx){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=(p.foot[i].x+wx-v.camera/256)/2;}
 if(s->stage){
  unsigned reach=s->stage==1?(unsigned)ht_clamp((int)s->tick-40,0,24)*256/24:256;
  ht_joint c=ht_recorder_latch();c.x-=v.camera/256;c.y-=v.camera_y/256;
  p.hand[1]=ht_recorder_mix(p.hand[1],c,reach);
 }
 return p;
}
static void ht_recorder_paper(int x,int y,int w,int h){
 ht_rect(ht_scene,x-3,y+3,w+6,h,100);ht_rect(ht_scene,x,y,w,h,30);
 ht_limb(ht_scene,x+2,y+h-1,x+w-1,y+h-1,1,1,74);
 /* Only the corners are worn: the sealed sheets have no wet stains. */
 ht_triangle(ht_scene,x+w-12,y+h,x+w,y+h-12,x+w,y+h,79);
 ht_limb(ht_scene,x+2,y+7,x+7,y+2,1,1,80);
}
static void ht_recorder_cabinet_insert(const ht_recorder_state *s){
 ht_rect(ht_scene,0,0,480,226,154);ht_rect(ht_scene,75,8,330,209,204);
 ht_rect(ht_scene,84,17,312,191,73);ht_rect(ht_scene,91,24,298,176,243);
 ht_rect(ht_scene,98,31,284,162,112);ht_limb(ht_scene,98,194,382,194,2,2,194);
 ht_text_color(104,37,"RUBBER SEAL",1,223);
 unsigned q=s->stage==3?ht_min(s->tick,96)*256/96:0;
 int x=166-(int)q*39/256,y=65-(int)q*18/256,w=141+(int)q*80/256,h=113+(int)q*25/256;
 ht_recorder_paper(x,y,w,h);ht_text_color(x+18,y+14,"RECORDER",1,191);ht_text_color(x+18,y+28,"MANUAL",1,191);
 for(int n=0;n<4;++n)ht_rect(ht_scene,x+21,y+51+n*10,w-47-n%2*19,1,128);
 /* Fingers stay at the same moving corners of the lifted manual. */
 ht_limb(ht_scene,78,239,x-7,y+h-13,11,7,231);ht_limb(ht_scene,x-7,y+h-13,x+9,y+h-9,6,2,224);
 ht_limb(ht_scene,412,239,x+w+7,y+h-21,11,7,230);ht_limb(ht_scene,x+w+7,y+h-21,x+w-7,y+h-17,6,2,222);
}
static void ht_recorder_joint(int x,int y){ht_disk(ht_scene,x,y,4,198);ht_disk(ht_scene,x,y,2,30);}
static void ht_recorder_front_page(void){
 ht_text_color(107,28,"RECORDER / ARRANGEMENT",1,194);
 ht_text_color(106,49,"MASTER PLATE",1,189);ht_rect(ht_scene,112,65,77,49,179);ht_rect(ht_scene,115,68,71,43,30);
 for(int n=0;n<4;++n)ht_limb(ht_scene,123,77+n*8,176,74+n*8,0,0,158);
 ht_limb(ht_scene,189,85,222,85,0,0,189);
 ht_text_color(271,50,"WRITING ARM",1,189);
 ht_limb(ht_scene,222,85,278,73,2,2,191);ht_limb(ht_scene,278,73,311,102,2,2,191);
 ht_recorder_joint(222,85);ht_recorder_joint(278,73);ht_recorder_joint(311,102);
 ht_limb(ht_scene,311,102,318,128,1,1,199);ht_limb(ht_scene,288,132,339,132,0,0,155);
 ht_rect(ht_scene,191,126,56,23,181);ht_rect(ht_scene,194,129,50,17,30);
 for(int n=0;n<4;++n)ht_rect(ht_scene,199+n*10,132,4,9,n==2?189:100);
 ht_limb(ht_scene,218,126,222,91,0,0,189);ht_limb(ht_scene,133,136,190,136,0,0,189);
 ht_text_color(107,157,"LIVE FEED -> SELECTOR",1,191);
 ht_text_color(105,181,"A SIGNATURE PROVES A PATTERN,",1,194);ht_text_color(105,193,"NOT A PERSON.",1,194);
}
/* These two diagrams share a single cross-link in each drawn enclosure.
 * Their opposite closures are printed states, not working shutters. */
static void ht_recorder_faces(int x,bool feed){
 ht_rect(ht_scene,x,95,128,89,175);ht_rect(ht_scene,x+2,97,124,85,30);
 ht_limb(ht_scene,x+15,107,x+113,107,1,1,198);ht_recorder_joint(x+64,107);
 for(int side=0;side<2;++side){
  int sx=x+9+side*65;bool closed=side!=(feed?1:0);
  ht_limb(ht_scene,sx+21,108,sx+21,120,0,0,183);
  ht_rect(ht_scene,sx,120,45,51,173);ht_rect(ht_scene,sx+2,122,41,47,30);
  if(closed){for(int n=0;n<7;++n)ht_limb(ht_scene,sx+3,126+n*6,sx+41,126+n*6,1,1,184);}
  else if(!side){for(int n=0;n<4;++n)ht_limb(ht_scene,sx+8,132+n*8,sx+34,128+n*8,0,0,168);}
  else{ht_limb(ht_scene,sx+8,136,sx+22,129,1,1,192);ht_limb(ht_scene,sx+22,129,sx+31,152,1,1,192);ht_recorder_joint(sx+22,129);ht_limb(ht_scene,sx+31,152,sx+34,161,0,0,193);}
 }
 ht_text_color(x+5,190,"PLATE",1,185);ht_text_color(x+76,190,"PEN",1,185);
}
/* Dotted arc on the same paper: shared arm -> shut selector -> other face. */
static ht_joint ht_recorder_trace(unsigned tick){
 static const ht_joint path[]={{163,107},{186,112},{206,135},{202,160},{176,175},{138,168},{131,140}};
 unsigned t=ht_min(tick,144),leg=ht_min(t/24,5),q=(t-leg*24)*256/24;
 return ht_recorder_mix(path[leg],path[leg+1],q);
}
static void ht_recorder_linkage_page(void){
 ht_text_color(102,27,"LEAVE MY WORDS FOR THE NEXT WATCH.",1,187);
 ht_text_color(102,40,"I CANNOT BE IN EVERY PLACE",1,187);ht_text_color(102,53,"THAT NEEDS AN ANSWER.",1,187);
 ht_text_color(108,77,"FEED CUT",1,194);ht_text_color(263,77,"FEED LIVE",1,194);
 ht_recorder_faces(99,false);ht_recorder_faces(253,true);
 for(unsigned t=0;t<=144;t+=8){ht_joint p=ht_recorder_trace(t);ht_disk(ht_scene,p.x,p.y,1,205);}
 /* A clean anonymous oval interrupts the dusty margin. No ownership cue. */
 for(int n=0;n<30;++n){int x=393+(int)(ht_hash(n+93)%26),y=78+(int)(ht_hash(n+801)%83);ht_rect(ht_scene,x,y,1,1,107);}
 for(int y=-10;y<=10;++y){int w=8-ht_abs(y)/3;ht_rect(ht_scene,405-w,122+y,w*2,1,30);}
 ht_text_color(101,207,"ONE RELEASE CLOSES THE OTHER.",1,181);
}
static void ht_recorder_manual(const ht_recorder_state *s){
 ht_rect(ht_scene,0,0,480,226,112);ht_recorder_paper(88,14,340,207);
 if(s->stage<6)ht_recorder_front_page();else ht_recorder_linkage_page();
 ht_limb(ht_scene,57,239,82,176,12,8,231);ht_limb(ht_scene,82,176,98,172,6,2,222);
 if(s->stage==5){
  int edge=426-(int)ht_min(s->tick,64)*330/64;
  ht_triangle(ht_scene,edge,17,426,17,edge,217,47);ht_limb(ht_scene,edge,17,edge,217,1,1,97);
  ht_limb(ht_scene,438,239,edge+14,189,10,6,231);ht_limb(ht_scene,edge+14,189,edge,186,5,2,223);
 }else if(s->stage>=7){
  ht_joint tip=ht_recorder_trace(s->stage==7?s->tick:144);
  /* Bent tip meets the printed dotted arc; it never touches a machine. */
  ht_joint bend=ht_joint_at(tip.x+7,tip.y+2),h=ht_joint_at(tip.x+37,tip.y+29);
  ht_limb(ht_scene,384,244,h.x+17,h.y+12,11,7,233);ht_limb(ht_scene,h.x+17,h.y+12,h.x,h.y,7,5,225);
  ht_limb(ht_scene,h.x+3,h.y+3,h.x-8,h.y-8,4,3,182);
  ht_limb(ht_scene,h.x-8,h.y-8,bend.x,bend.y,2,1,125);ht_limb(ht_scene,bend.x,bend.y,tip.x,tip.y,1,0,153);
 }else{ht_limb(ht_scene,424,239,428,180,11,7,231);ht_limb(ht_scene,428,180,416,177,5,2,223);}
}
static void ht_recorder_study_render(const ht_game *game,const ht_recorder_state *s){
 ht_game v=*game;v.camera=(HT_RECORDER_X-230)*256;v.camera_y=(ht_recorder_floor()-190)*256;
 v.intimacy=256;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);
 ht_world_scale=ht_scene_scale(&v);
 if(s->stage<2){
  unsigned opening=s->stage==1?(unsigned)ht_clamp((int)s->tick-64,0,32)*256/32:0;
  ht_recorder_cabinet(&v,opening);ht_person_pose p=ht_recorder_pose(&v,s);ht_person_draw(&p);
 }
 ht_world_scale=256;
 if(s->stage==2 || s->stage==3)ht_recorder_cabinet_insert(s);
 else if(s->stage>=4)ht_recorder_manual(s);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_recorder_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A OPEN  START MANUAL  X RETURN":s->stage==2?"A LIFT MANUAL  START MANUAL  X RETURN":s->stage==4?"A TURN PAGE  START MANUAL  X RETURN":s->stage==6?"A TRACE LINKAGE  START MANUAL  X RETURN":s->stage==8?"A READ MANUAL  START MANUAL  X RETURN":"PAUSE HOLD  START MANUAL  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
