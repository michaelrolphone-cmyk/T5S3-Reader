/* Quiet first-landing pause and final-stair evidence. Local clocks and poses
 * never move the live body, boat, puzzle or cabinet/read state. */
typedef struct {bool active,paused,card;uint8_t stage;uint16_t tick;} ht_tower_state;
static ht_tower_state ht_tower;
static bool ht_tower_near(const ht_game *g){return g->level==9 && g->grounded && !g->traversal.mode && !g->door_stage && ht_abs(g->x/256-HT_TOWER_BENCH_X)<=22 && ht_abs(g->y/256-ht_surface_at(g,3,g->x/256))<=3;}
static bool ht_tower_card_near(const ht_game *g){return g->level==9 && g->grounded && !g->traversal.mode && !g->door_stage && ht_evidence_near(g)==1;}
static void ht_tower_begin(bool card){ht_tower=(ht_tower_state){true,false,card,0,0};}
static unsigned ht_tower_limit(const ht_tower_state *s){return (s->stage&1)?(s->stage==1?64:96):0;}
static bool ht_tower_step(ht_tower_state *s){
 if(!s->active || s->paused || !ht_tower_limit(s))return false;
 if(++s->tick==ht_tower_limit(s)){++s->stage;s->tick=0;}return true;
}
static bool ht_tower_action(ht_tower_state *s){if(!s->active || s->paused || (s->stage&1) || s->stage>=(s->card?6:10))return false;++s->stage;s->tick=0;return true;}
static const char *ht_tower_label(const ht_tower_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->card){
  if(!s->stage)return "THE RADIO CARD LAY BESIDE THE LAST STAIR.";
  if(s->stage==1)return "A BRASS CLIP HELD IT AGAINST THE WALL.";
  if(s->stage==2)return "THREE SHORT FLASHES. RELEASE BETWEEN EACH.";
  if(s->stage==3)return "UNDER THE CARD WAS A SERVICE RECORD.";
  if(s->stage==4)return "SEALED ON THE FIRST DAY. LOCKS FITTED ON THE FOURTH.";
  if(s->stage==5)return "HE HAD SIGNED FOR THE LOCKS.";
  return "THE PEN HAD PIERCED THE PAPER. I TOOK THE RECORD.";
 }
 if(!s->stage)return "I STOPPED SO SUDDENLY THAT THE BAG SWUNG.";
 if(s->stage==1 || s->stage==2)return "SEVEN HOOKS. AN EIGHTH FIXED LATER.";
 if(s->stage==3)return "I SAT ON THE BENCH BEFORE GOING FARTHER.";
 if(s->stage==4)return "ONLY OLD WOOD AND A PALE SQUARE ON THE WALL.";
 if(s->stage==5)return "I TOUCHED A WHITE CHIP. IT FELL TO POWDER.";
 if(s->stage==6)return "MY FEET HURT LESS. MY RIBS HURT MORE.";
 if(s->stage==7)return "THE SCARF HAD DRIED ROUGH AGAINST MY NECK.";
 if(s->stage==8)return "I HAD BEEN WEARING THE REMAINDER.";
 if(s->stage==9)return "I FOLDED IT AND PUT IT INSIDE MY COAT.";
 return "AT HOME SHE WOULD HAVE MENDED IT.";
}
static ht_joint ht_tower_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_tower_pose(const ht_game *game,const ht_tower_state *s){
 ht_game v=*game;int cx=v.camera/256,cy=v.camera_y/256;
 unsigned q=s->stage?256:0;if(s->stage==1)q=ht_min(s->tick,32)*8;
 int target=s->card?ht_tower_card_contact().x-5:HT_TOWER_BENCH_X+27;
 int wx=game->x/256+(target-game->x/256)*(int)q/256;
 v.x=wx*256;v.y=ht_surface_at(&v,s->card?8:3,wx)*256;v.vx=s->stage==1 && s->tick<32?232:0;v.vy=0;v.facing=1;v.grounded=true;v.landing_ticks=0;v.stride=s->tick*96;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(wx-cx,v.y/256-cy,&v);
 if(s->card && s->stage==1){ht_joint c=ht_tower_card_contact();c.x-=cx;c.y-=cy;p.hand[1]=ht_tower_mix(p.hand[1],c,(unsigned)ht_clamp((int)s->tick-32,0,24)*256/24);}
 if(!s->card && s->stage>=3){
  unsigned sit=s->stage==3?ht_min(s->tick,64)*4:256;int f=ht_tower_floor()-cy;
  p.hip=ht_tower_mix(p.hip,ht_joint_at(target-cx,f-11),sit);
  p.shoulder=ht_tower_mix(p.shoulder,ht_joint_at(target+1-cx,f-19),sit);
  for(int i=0;i<2;++i){int fx=target+5+i*2;
   p.foot[i]=ht_tower_mix(p.foot[i],ht_joint_at(fx-cx,ht_surface_at(&v,3,fx)-cy),sit);
   p.hand[i]=ht_tower_mix(p.hand[i],ht_joint_at(target+3+i*3-cx,f-10),sit);
  }
 }
 if(!s->card && s->stage==5){ht_joint c=ht_tower_chip();c.x-=cx;c.y-=cy;p.hand[1]=ht_tower_mix(p.hand[1],c,ht_min(s->tick,24)*256/24);}
 return p;
}
/* The insert is a four-times view of the same hook/chip coordinates. */
static ht_joint ht_tower_hook_detail(ht_joint p){return ht_joint_at(240+(p.x-HT_TOWER_BENCH_X)*4,224+(p.y-ht_tower_floor())*4);}
static void ht_tower_hand(ht_joint h,bool right){
 int x=right?429:48;ht_limb(ht_scene,x,246,h.x+(right?15:-15),h.y+24,13,7,232);
 ht_limb(ht_scene,h.x+(right?15:-15),h.y+24,h.x,h.y,6,2,219);ht_disk(ht_scene,h.x,h.y,3,215);
}
static ht_joint ht_tower_chip_hand(const ht_tower_state *s){
 ht_joint c=ht_tower_hook_detail(ht_tower_chip());
 return ht_tower_mix(ht_joint_at(c.x+24,210),c,s->stage>5?256:ht_min(s->tick,32)*8);
}
static void ht_tower_hook_insert(const ht_tower_state *s){
 ht_rect(ht_scene,0,0,480,226,112);ht_rect(ht_scene,56,61,325,19,177);
 ht_rect(ht_scene,200,100,88,84,64); /* blank sheltered square, no invented initial */
 for(unsigned n=0;n<8;++n){ht_joint h=ht_tower_hook_detail(ht_tower_hook(n));
  if(n==7)for(int k=0;k<8;++k){if(k==2 && s->stage>=5 && (s->stage>5 || s->tick>=32))continue;ht_rect(ht_scene,h.x-14+k*4,h.y-15+(k%3)*9,4,5,48);}
  ht_limb(ht_scene,h.x,h.y-11,h.x,h.y+8,3,3,224);ht_limb(ht_scene,h.x,h.y+8,h.x+10,h.y+5,3,1,230);ht_disk(ht_scene,h.x,h.y-9,2,89);
 }
 ht_rect(ht_scene,38,181,397,13,204);ht_rect(ht_scene,48,195,9,31,223);ht_rect(ht_scene,416,195,9,31,223);
 if(s->stage>=5){
  ht_joint c=ht_tower_hook_detail(ht_tower_chip());unsigned dust=s->stage>5?64:(unsigned)ht_clamp((int)s->tick-32,0,64);
  if(!dust)ht_rect(ht_scene,c.x-2,c.y-2,5,5,45);
  else for(int n=0;n<11;++n){int y=c.y+(int)dust*(106+(n%3)*3)/64;ht_rect(ht_scene,c.x-14+n*3,ht_min(y,181+n%3),1+n%2,1,60+n%3*9);}
  if(s->stage==5)ht_tower_hand(ht_tower_chip_hand(s),true);
 }
 ht_checkpoint();
}
/* Both hands share the moving cloth endpoints; folding shortens the same
 * remnant before it disappears into the coat. No new scarf piece is created. */
static ht_joint ht_tower_scarf(const ht_tower_state *s){
 ht_joint neck=ht_joint_at(164,38),knee=ht_joint_at(117,125),pocket=ht_joint_at(345,61);
 if(s->stage==7)return ht_tower_mix(neck,knee,ht_min(s->tick,64)*4);
 if(s->stage>=9)return ht_tower_mix(knee,pocket,s->stage>9?256:(unsigned)ht_clamp((int)s->tick-56,0,40)*256/40);
 return knee;
}
static int ht_tower_scarf_width(const ht_tower_state *s){
 if(s->stage==7)return 86+(int)ht_min(s->tick,96)*153/96;
 if(s->stage>=9)return 239-(int)(s->stage>9?56:ht_min(s->tick,56))*210/56;
 return 239;
}
static ht_joint ht_tower_scarf_hand(const ht_tower_state *s,bool right){ht_joint p=ht_tower_scarf(s);return ht_joint_at(p.x+(right?ht_tower_scarf_width(s):0),p.y+7);}
static void ht_tower_scarf_insert(const ht_tower_state *s){
 ht_rect(ht_scene,0,0,480,226,109);ht_triangle(ht_scene,81,226,140,107,231,226,220);ht_triangle(ht_scene,232,226,311,108,407,226,216);
 ht_rect(ht_scene,283,0,153,80,221);ht_limb(ht_scene,307,76,391,70,2,2,184);
 ht_joint p=ht_tower_scarf(s);int w=ht_tower_scarf_width(s);bool visible=s->stage<10;
 if(visible){
  ht_rect(ht_scene,p.x,p.y,w,14,139);
  for(int n=0;n<w;n+=7)ht_limb(ht_scene,p.x+n,p.y+1,p.x+n+3,p.y+11,0,0,105);
  for(int n=0;n<5;++n){ht_limb(ht_scene,p.x,p.y+n*3,p.x-5-n%3,p.y+n*3+2,0,0,132);ht_limb(ht_scene,p.x+w,p.y+n*3,p.x+w+4+n%2,p.y+n*3-1,0,0,132);}
  if(s->stage==9 && s->tick<56)ht_limb(ht_scene,p.x+w/2,p.y,p.x+w/2,p.y+13,1,1,185);
 }
 ht_tower_hand(ht_tower_scarf_hand(s,false),false);ht_tower_hand(ht_tower_scarf_hand(s,true),true);
 if(s->stage==10 || (s->stage==9 && s->tick>=88))ht_rect(ht_scene,336,57,51,18,221);
 ht_checkpoint();
}
static ht_joint ht_tower_radio_sheet(const ht_tower_state *s,bool record){
 ht_joint fixed=ht_joint_at(record?153:140,record?48:31),taken=ht_joint_at(record?238:8,record?78:45);
 unsigned q=record?(s->stage>5?256:s->stage==5?ht_min(s->tick,80)*256/80:0):(s->stage>3?256:s->stage==3?ht_min(s->tick,80)*256/80:0);
 return ht_tower_mix(fixed,taken,q);
}
static ht_joint ht_tower_radio_hand(const ht_tower_state *s,bool record){ht_joint p=ht_tower_radio_sheet(s,record);return ht_joint_at(p.x+(record?169:3),p.y+120);}
static void ht_tower_radio_insert(const ht_tower_state *s){
 ht_rect(ht_scene,0,0,480,226,144);ht_rect(ht_scene,116,14,244,190,178);
 for(int n=0;n<2;++n){bool record=n==0;ht_joint p=ht_tower_radio_sheet(s,record);int x=p.x,y=p.y;
  int width=record?176:144;ht_rect(ht_scene,x+2,y+2,width+2,132,214);ht_rect(ht_scene,x,y,width,130,record?77:42);
  if(record){
   ht_text_color(x+9,y+12,"SERVICE RECORD",1,209);ht_text_color(x+9,y+34,"DAY 1 - MASTER SEALED",1,217);ht_text_color(x+9,y+50,"DAY 4 - HOLDING LOCKS",1,217);
   for(int k=0;k<9;++k)ht_limb(ht_scene,x+19+k*13,y+87+(k%2)*4,x+30+k*13,y+84+(k%3)*3,1,0,221);
   ht_disk(ht_scene,x+147,y+88,2,234);ht_limb(ht_scene,x+145,y+87,x+148,y+91,0,0,31); /* pierced end */
  }else{
   const char *lines[]={"THREE SHORT FLASHES.","FIRST LAMP. RELEASE.","YOU WILL KNOW ME", "BY THE CORRECTION.","CORRECTIONS ARE", "COPIED TOO."};
   for(int k=0;k<6;++k)ht_text_color(x+9,y+12+k*17,lines[k],1,k<4?209:166);
  }
 }
 /* The brass clip remains attached as both papers leave it. */
 ht_limb(ht_scene,225,20,225,42,3,3,199);ht_limb(ht_scene,225,42,240,42,3,3,219);ht_disk(ht_scene,225,22,2,72);
 if(s->stage>=3)ht_tower_hand(ht_tower_radio_hand(s,false),false);
 if(s->stage>=5)ht_tower_hand(ht_tower_radio_hand(s,true),true);
 ht_checkpoint();
}
static void ht_tower_study_render(const ht_game *game,const ht_tower_state *s){
 ht_game v=*game;int wx=s->card?ht_tower_card_contact().x:HT_TOWER_BENCH_X;
 int f=s->card?ht_land_height(9,8,wx):ht_tower_floor();v.camera=(wx-239)*256;v.camera_y=(f-183)*256;
 v.intimacy=256;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 bool overview=s->card?s->stage<2:s->stage==0 || s->stage==3 || s->stage==4 || (s->stage==1 && s->tick<32) || (s->stage==5 && s->tick<32);
 if(overview){ht_person_pose p=ht_tower_pose(&v,s);ht_person_draw(&p);
  /* Carried bag settles after the abrupt stop, then rests against the bench. */
  if(!s->card){int swing=s->stage?0:3;ht_limb(ht_scene,p.shoulder.x-1,p.shoulder.y,p.hip.x-5-swing,p.hip.y+1,0,0,169);ht_rect(ht_scene,p.hip.x-9-swing,p.hip.y-1,7,8,182);}
 }
 ht_world_scale=256;
 if(!overview){if(s->card)ht_tower_radio_insert(s);else if(s->stage<=6)ht_tower_hook_insert(s);else ht_tower_scarf_insert(s);}
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_tower_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys;
 if(s->card)keys=!s->stage?"A REACH CARD  START NOTE  X RETURN":s->stage==2?"A LIFT CARD  START NOTE  X RETURN":s->stage==4?"A TAKE RECORD  START NOTE  X RETURN":s->stage==6?"A READ NOTE  START NOTE  X RETURN":"PAUSE HOLD  START NOTE  X RETURN";
 else keys=!s->stage?"A LOOK AT HOOKS  START JOURNAL  X RETURN":s->stage==2?"A SIT DOWN  START JOURNAL  X RETURN":s->stage==4?"A TOUCH CHIP  START JOURNAL  X RETURN":s->stage==6?"A REMOVE SCARF  START JOURNAL  X RETURN":s->stage==8?"A FOLD SCARF  START JOURNAL  X RETURN":s->stage==10?"START JOURNAL  X RETURN":"PAUSE HOLD  START JOURNAL  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_tower_prompt(const ht_game *g){if(!ht_tower_near(g))return;ht_rect(ht_scene,126,221,229,15,255);ht_text_color(138,225,"A PAUSE BENEATH THE EIGHT HOOKS",1,85);}
