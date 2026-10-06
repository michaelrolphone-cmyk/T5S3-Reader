#define HT_DISTRIBUTION_STORY 1
typedef struct {bool active,paused;int8_t direction;uint8_t stage,tick,overlap;} ht_distribution_state;
static ht_distribution_state ht_distribution;
static bool ht_distribution_near(const ht_game *g) {
 return g->level==2 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==2;
}
static void ht_distribution_begin(void){ht_distribution=(ht_distribution_state){true,false,0,0,0,0};}
/* Each gesture ends in a quiet hold. B/Up requests the next one; A/Start
 * retain the existing reading controls. No gesture changes live gameplay. */
static unsigned ht_distribution_limit(unsigned stage){
 return stage==2?128:stage==4?96:stage==6?160:stage==8?96:stage==10?144:0;
}
static bool ht_distribution_continue(ht_distribution_state *s){
 if(!s->active || s->paused || !s->stage || ht_distribution_limit(s->stage))return false;
 if(s->stage==11)s->stage=1;else ++s->stage;
 s->tick=0;s->direction=0;return true;
}
static bool ht_distribution_step(ht_distribution_state *s){
 if(!s->active || s->paused)return false;
 if(!s->stage && s->tick<128){++s->tick;return true;}
 if(s->stage==1){int old=s->overlap;s->overlap=(uint8_t)ht_clamp(old+s->direction*2,0,64);return old!=s->overlap;}
 unsigned limit=ht_distribution_limit(s->stage);
 if(limit){if(++s->tick==limit){++s->stage;s->tick=0;}return true;}
 return false;
}
static bool ht_distribution_take(ht_distribution_state *s){
 if(!s->active || s->paused || s->stage || s->tick<128)return false;
 s->stage=1;s->overlap=0;s->direction=0;return true;
}
static const char *ht_distribution_label(const ht_distribution_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return s->tick<80?"THE CAN HANDLES WERE TIED TOGETHER.":s->tick<128?"I TOUCHED ONE CAN. THE WHOLE ROW MOVED.":"TWO LISTS, ROLLED INSIDE THE LAST CAN.";
 if(s->stage==2)return "A GUST LIFTED THE BLANK LIST.";
 if(s->stage==3)return "I CAUGHT THAT ONE FIRST. I STAYED SITTING.";
 if(s->stage<6 && s->stage>3)return "I TOOK OUT THE NOTEBOOK.";
 if(s->stage==6 || s->stage==7)return "I WROTE DOWN BOTH DATES. MY HAND WAS CLUMSY WITH COLD.";
 if(s->stage==8 || s->stage==9)return "THE FIRST LETTER OF MY NAME LOOKED STRANGE TO ME.";
 if(s->stage>=10)return "I RUBBED AT THE CROSSING UNTIL THE PAPER BEGAN TO PILL.";
 return s->overlap>48?"THE INNER LIST WAS DATED BEFORE HER ARRIVAL.":"LEFT: AN EMPTY EIGHTH LINE. RIGHT: MY INITIAL.";
}
static void ht_distribution_initial(int x,int y){
 ht_limb(ht_scene,x+14,y+94,x+17,y+90,1,1,231);
 ht_limb(ht_scene,x+17,y+90,x+22,y+94,1,0,231);
}
static void ht_distribution_sheet(int x,int y,bool inner){
 ht_triangle(ht_scene,x,y,x+112,y+3,x+108,y+115,inner?74:44);
 ht_triangle(ht_scene,x,y,x+108,y+115,x-3,y+109,inner?74:44);
 ht_rect(ht_scene,x+8,y+13,75,1,143); /* date strokes, no fabricated numerals */
 for(int row=0;row<8;++row){
  int yy=y+28+row*10;
  ht_rect(ht_scene,x+7,yy,93,1,133);
  if(row<7)for(int n=0;n<4;++n)
   ht_limb(ht_scene,x+13+n*16,yy-3,x+20+n*16,yy-5+(row+n)%3,1,0,188);
 }
 if(inner){
  /* The unnamed narrator's initial is an incomplete ink mark, not an
   * invented name or printed letter. Its presence distinguishes line eight. */
  ht_distribution_initial(x,y);
 }
}
/* Paper and catching fingers use the same moving contact. The inner list
 * stays on the right knee throughout the gust; only the blank outer lifts. */
static ht_joint ht_distribution_blank(const ht_distribution_state *s){
 int left=95+s->overlap,y=67;
 if(s->stage==2){int t=s->tick,lift=t<48?t:t<80?48:48-(t-80)*24/48;
  left-=lift/2;y-=lift;
 }else if(s->stage>=3){left-=12;y-=24;}
 if(s->stage>=4){int q=s->stage==4?ht_min(s->tick,32):32;left+=(95-left)*q/32;y+=(83-y)*q/32;}
 return ht_joint_at(left,y);
}
static ht_joint ht_distribution_catch(const ht_distribution_state *s){
 ht_joint paper=ht_distribution_blank(s);int x=paper.x+8,y=paper.y+99;
 if(s->stage==2 && s->tick<48){int lag=s->tick<24?s->tick:48-s->tick;x+=lag/2;y+=lag;}
 return ht_joint_at(x,y);
}
static ht_joint ht_distribution_pen(const ht_distribution_state *s){
 int x=265,y=181,t=s->tick;
 if(s->stage==6){
  if(t<112){int row=t/56,part=t%56;x=171+ht_min(part,40);y=112+row*22+(part/5)%3;}
  else if(t<136){int q=t-112;x=173+4*q/24;y=174-5*q/24;}
  else {int q=ht_min(t-136,24);x=177+4*q/24;y=169+6*q/24;}
 }else if(s->stage==8){int q=ht_min(t,64);x=170+q*18/64;y=169+q*9/64;}
 else if(s->stage==10){x=176+(t/6)%5;y=173+(t/9)%3;}
 return ht_joint_at(x,y);
}
static void ht_distribution_notebook(const ht_distribution_state *s){
 int t=s->tick,open=s->stage==4?ht_clamp(t-32,0,64):64;
 int top=213-open*150/64;
 ht_rect(ht_scene,127,top-4,228,149,220);ht_rect(ht_scene,134,top,106,140,49);
 ht_rect(ht_scene,242,top,105,140,67);ht_limb(ht_scene,240,top+1,240,top+137,1,1,157);
 for(int n=0;n<5;++n){ht_rect(ht_scene,139,top+135+n,95,1,94+n*9);ht_rect(ht_scene,248,top+135+n,94,1,111+n*9);}
 if(s->stage>=6){
  /* Date and initial gestures intentionally never supply a calendar value,
   * legible name or new prose. The source is available through A/Start. */
  for(int row=0;row<2;++row){int progress=s->stage==6?ht_clamp(t-row*56,0,40):40;
   for(int n=0;n<progress;n+=5)ht_limb(ht_scene,171+n,top+49+row*22,174+n,top+47+row*22+(n%3),1,0,177);
  }
  if(s->stage>6 || t>=112){
   int q=s->stage==6?ht_clamp(t-112,0,24):24;
   ht_limb(ht_scene,173,top+111,173+4*q/24,top+111-5*q/24,1,1,205);
   if(s->stage>6 || t>=136){q=s->stage==6?ht_clamp(t-136,0,24):24;
    ht_limb(ht_scene,177,top+106,177+4*q/24,top+106+6*q/24,1,0,205);
   }
  }
  if(s->stage>=8){int q=s->stage==8?ht_min(t,64):64;
   ht_limb(ht_scene,170,top+106,170+q*18/64,top+106+q*9/64,1,1,230);
  }
  if(s->stage>=10){int count=s->stage==10?ht_min(t/8,16):16;
   for(int n=0;n<count;++n){int x=171+(n*7)%19,y=top+106+(n*5)%13;
    ht_limb(ht_scene,x,y,x+2,y-1,1,0,n&1?114:39);
   }
  }
 }
 /* The left thumb holds the opened book against the knee. */
 ht_limb(ht_scene,94,222,142,top+120,7,4,235);ht_disk(ht_scene,142,top+117,4,224);
 ht_joint pen=ht_distribution_pen(s);
 if(s->stage==4){pen.x=354;pen.y=top+120;}
 ht_limb(ht_scene,381,223,pen.x+14,pen.y+12,8,5,237);
 ht_limb(ht_scene,pen.x+14,pen.y+12,pen.x+5,pen.y+3,5,3,221);
 if(s->stage==6 || s->stage==8){ht_limb(ht_scene,pen.x+13,pen.y-17,pen.x,pen.y,1,0,240);}
 else if(s->stage==10){ht_disk(ht_scene,pen.x,pen.y,3,223);ht_limb(ht_scene,pen.x+5,pen.y+3,pen.x,pen.y,3,2,223);}
}
static void ht_distribution_study_render(const ht_game *game,const ht_distribution_state *s){
 ht_native_active=ht_camera_mode==HT_CAMERA_NATIVE && ht_native_a;
 ht_native_foreground_half_y=false;ht_scene=ht_native_active?ht_native_a:ht_scene_low;ht_world_scale=256;
 for(int y=0;y<HT_H;++y){ht_rect(ht_scene,0,y,HT_W,1,110+y/7);if((y&15)==15)ht_checkpoint();}
 if(!s->stage){
  ht_game v=*game;v.camera=(HT_DISTRIBUTION_X-260)*256;v.camera_y=(ht_distribution_floor()-190)*256;
  ht_world_scale=384;
  for(int sx=-40;sx<HT_W+40;sx+=8){
   int yy=ht_surface_at(&v,7,v.camera/256+sx)-v.camera_y/256;
   ht_rect(ht_scene,sx,yy,8,ht_max(0,HT_H+80-yy),155);
  }
  ht_distribution_room(&v,s->tick<80?48:s->tick-80);
  /* Only the inspection view approaches the can; live position stays frozen. */
  int target=HT_DISTRIBUTION_X-6,from=game->x/256;
  v.x=(from+(target-from)*(int)ht_min(s->tick,80)/80)*256;
  v.y=ht_surface_at(&v,7,v.x/256)*256;v.vx=v.vy=0;v.facing=1;v.ticks=0;
  ht_person_pose p=ht_person_pose_at(v.x/256-v.camera/256,(v.y-v.camera_y)/256,&v);
  if(s->tick>=80){p.hand[0]=ht_joint_at(HT_DISTRIBUTION_X-v.camera/256+ht_distribution_can_offset(s->tick-80),p.shoulder.y-2);p.elbow[0]=1;}
  ht_person_draw(&p);
 }else{
  /* The low wall and two knees support the sheets; can rests between feet.
   * Beyond the tanks, the untraversed rail embankment remains only a view. */
  if(s->stage>=2){
   ht_triangle(ht_scene,30,56,454,38,463,51,131);ht_limb(ht_scene,35,52,453,35,1,1,147);
   for(int n=0;n<4;++n){int x=37+n*111;ht_rect(ht_scene,x,55,55,28,139);ht_limb(ht_scene,x,55,x+55,55,2,2,157);}
  }
  ht_rect(ht_scene,81,197,324,20,168);
  ht_limb(ht_scene,112,214,144,170,22,19,217);ht_disk(ht_scene,144,170,19,217);
  ht_limb(ht_scene,367,214,329,172,22,19,227);ht_disk(ht_scene,329,172,19,227);
  ht_rect(ht_scene,218,190,30,33,210);ht_rect(ht_scene,224,181,17,11,218);ht_rect(ht_scene,228,184,9,6,111);
  ht_joint blank=ht_distribution_blank(s),catching=ht_distribution_catch(s);
  int left=blank.x,right=267-s->overlap,right_y=70;
  if(s->stage>=4){int q=s->stage==4?ht_min(s->tick,32):32;right+=(267-right)*q/32;right_y+=13*q/32;}
  ht_distribution_sheet(left,blank.y,false);ht_distribution_sheet(right,right_y,true);
  /* Each hand follows its own sheet in comparison and placement; only the
   * outer hand reaches to catch the lifted paper during the gust. */
  if(s->stage<4 || (s->stage==4 && s->tick<32))for(int side=0;side<2;++side){
   int paper=side?right+98:catching.x,wrist=side?378:95,hand_y=side?right_y+99:catching.y;
   ht_limb(ht_scene,wrist,218,paper,hand_y+3,6,4,237);
   ht_disk(ht_scene,paper,hand_y,4,244);
   ht_limb(ht_scene,paper,hand_y+1,paper+(side?-5:5),hand_y-8,2,1,244);
  }
  if(s->stage==2 && s->tick<80)for(int n=0;n<12;++n){int x=(n*39+s->tick*3)%470,y=27+n*13;ht_limb(ht_scene,x,y,x+7,y-1,0,0,145);}
  if(s->stage>4 || (s->stage==4 && s->tick>=32))ht_distribution_notebook(s);
 }
 ht_world_scale=256;ht_rect(ht_scene,12,226,456,35,255);
 const char *label=ht_distribution_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,64);
 const char *keys=!s->stage?"A TAKE LISTS  START NOTE  X RETURN":s->stage==1?"L/R COMPARE  B/UP STAY  A READ  START NOTE  X RETURN":
  s->stage==3?"B/UP NOTEBOOK  A READ  START NOTE  X RETURN":s->stage==5?"B/UP COPY DATES  A READ  START NOTE  X RETURN":
  s->stage==7?"B/UP CROSS OUT  A READ  START NOTE  X RETURN":s->stage==9?"B/UP RUB PAPER  A READ  START NOTE  X RETURN":
  s->stage==11?"B/UP LISTS  A READ  START NOTE  X RETURN":"PAUSE HOLD  A READ  START NOTE  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,100);ht_vignette();
}
