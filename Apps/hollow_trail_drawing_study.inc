#define HT_DRAWING_STUDY 1
typedef struct {bool active,paused;int8_t direction,shift;uint8_t stage;uint16_t tick;} ht_drawing_state;
static ht_drawing_state ht_drawing;
static bool ht_drawing_near(const ht_game *g){return g->level==6 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==2;}
static void ht_drawing_begin(void){ht_drawing=(ht_drawing_state){true,false,0,0,0,0};}
static bool ht_drawing_step(ht_drawing_state *s){
 if(!s->active || s->paused)return false;
 if(s->stage==2){int old=s->shift;s->shift=(int8_t)ht_clamp(old+s->direction,-20,94);return old!=s->shift;}
 if(!(s->stage&1))return false;
 if(++s->tick==96){++s->stage;s->tick=0;}return true;
}
static bool ht_drawing_action(ht_drawing_state *s){if(!s->active || s->paused || (s->stage&1) || s->stage>=8)return false;++s->stage;s->tick=0;s->direction=0;return true;}
static const char *ht_drawing_label(const ht_drawing_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "A CHILD'S DRAWING HUNG BESIDE AN OLDER CARD.";
 if(s->stage==1)return "I HELD THE CHILD'S DRAWING CLOSER.";
 if(s->stage==2)return "THE PICTURE CAME BEFORE THE PROMISE.";
 if(s->stage==3 || s->stage==4)return "I SET THE DOG FROM THE RAILWAY BESIDE THEM.";
 if(s->stage==5)return "THEN I CAUGHT MYSELF AND PUT THE DOG AWAY.";
 if(s->stage==6)return "THE SMALLEST LEANED AGAINST THE WOMAN'S SKIRT.";
 return "THE LIGHT'S ROUTE WAS DRAWN IN THE MARGIN.";
}
/* Separate authored paths retain a child's revisions rather than cloning the
 * mechanically regular older card. No invented print date or identity proof. */
static void ht_drawing_picture(int x,int y,bool child){
 int ink=child?216:171,base=y+115,wx=x+31,head=base-45;
 if(!child)ht_limb(ht_scene,x+12,base,x+164,base,1,1,126);
 ht_disk(ht_scene,wx,head,6,ink);ht_limb(ht_scene,wx,head+7,wx-2,base-19,2,2,ink);
 ht_triangle(ht_scene,wx-2,base-30,wx-10,base-9,wx+9,base-9,ink);
 ht_limb(ht_scene,wx-6,base-9,wx-6,base,1,1,ink);ht_limb(ht_scene,wx+5,base-9,wx+5,base,1,1,ink);
 ht_limb(ht_scene,wx-6,base,wx+(child?-12:-2),base,1,1,ink);ht_limb(ht_scene,wx+5,base,wx+10,base+(child?-3:0),1,1,ink);
 ht_limb(ht_scene,wx-5,head+9,wx+10,head+8,2,1,ink);ht_limb(ht_scene,wx-4,head+9,wx-11,head+18,2,1,ink);ht_limb(ht_scene,wx+7,head+9,wx+11,head+25,2,1,ink);
 int lx=wx-(child?19:14),ly=child?y+17:y+41,lw=child?26:18,lh=child?50:24;
 ht_limb(ht_scene,wx-1,head+13,lx-9,ly+lh/2,1,1,ink);ht_limb(ht_scene,lx-9,ly+lh/2,lx-3,ly+lh/2-8,1,1,ink);
 ht_rect(ht_scene,lx-lw/2,ly,lw,lh,ink);ht_rect(ht_scene,lx-lw/2+3,ly+3,lw-6,lh-6,64);
 ht_limb(ht_scene,lx-5,ly,lx-4,ly-6,1,1,ink);ht_limb(ht_scene,lx-4,ly-6,lx+5,ly-6,1,1,ink);ht_limb(ht_scene,lx+5,ly-6,lx+5,ly,1,1,ink);
 static const int16_t spacing[6]={49,72,91,118,137,158};
 static const int8_t height[6]={23,46,31,36,24,34};
 if(child){ht_limb(ht_scene,lx+lw/2,ly+12,x+171,y+57,2,1,130);ht_limb(ht_scene,lx+lw/2,ly+28,x+170,base-4,2,1,132);}
 for(int i=0;i<6;++i){int px=x+(child?spacing[i]:58+i*20),h=child?height[i]:35,feet=base-(child?(i%3):0),top=feet-h;
  if(child && i==1){ /* The shorter second figure survives as a rubbed ghost. */
   ht_disk(ht_scene,px-3,feet-27,4,95);ht_limb(ht_scene,px-3,feet-23,px-5,feet-8,1,1,96);ht_limb(ht_scene,px-5,feet-8,px-9,feet,1,1,96);
   ht_limb(ht_scene,px-10,feet-30,px+3,feet-15,2,2,64);
  }
  ht_disk(ht_scene,px,top,4,ink);ht_limb(ht_scene,px,top+5,px-1,feet-10,1,1,ink);
  ht_limb(ht_scene,px-1,feet-10,px-5,feet,1,1,ink);ht_limb(ht_scene,px-1,feet-10,px+5,feet,1,1,ink);
  int hand=px+(child && !i?-12:7),hy=feet-h/2;
  ht_limb(ht_scene,px,top+8,hand,hy,1,1,ink);ht_limb(ht_scene,px,top+8,px-7,hy+2,1,1,ink);
  if(child && i==2)for(int finger=0;finger<6;++finger)ht_limb(ht_scene,hand,hy,hand+4+(finger%2),hy-5+finger*2,0,0,ink);
 }
 if(child){
  /* Hard lantern strokes cross people; the child draws them back over the rays. */
  ht_limb(ht_scene,x+88,y+83,x+91,y+84,1,1,ink);ht_limb(ht_scene,x+116,y+79,x+120,y+83,1,1,ink);
  ht_limb(ht_scene,x+12,y+132,x+39,y+124,1,0,185);ht_limb(ht_scene,x+39,y+124,x+49,y+128,1,0,185); /* Margin route, not a new solution. */
 }else{ht_limb(ht_scene,x+16,y+129,x+77,y+129,0,0,131);ht_limb(ht_scene,x+16,y+132,x+58,y+132,0,0,131);}
}
static void ht_drawing_dog(int x,int y,unsigned q){
 if(!q)return;
 int top=y+(256-(int)q)*70/256;
 ht_rect(ht_scene,x,top,104,72,55);ht_limb(ht_scene,x+3,top+36,x+100,top+36,1,1,124);
 ht_limb(ht_scene,x+17,top+24,x+73,top+23,1,1,216);ht_limb(ht_scene,x+17,top+24,x+19,top+38,1,1,216);ht_limb(ht_scene,x+19,top+38,x+74,top+38,1,1,216);ht_limb(ht_scene,x+74,top+38,x+75,top+23,1,1,216);
 ht_limb(ht_scene,x+17,top+24,x+8,top+12,1,1,216);ht_limb(ht_scene,x+73,top+23,x+75,top+15,1,1,216);ht_limb(ht_scene,x+75,top+15,x+86,top+13,1,1,216);ht_limb(ht_scene,x+86,top+13,x+96,top+24,1,1,216);ht_limb(ht_scene,x+96,top+24,x+75,top+28,1,1,216);
 ht_limb(ht_scene,x+76,top+15,x+77,top+7,1,1,216);ht_limb(ht_scene,x+77,top+7,x+82,top+14,1,1,216);
 for(int n=0;n<5;++n){int xx=x+23+n*11;ht_limb(ht_scene,xx,top+38,xx+2,top+62+n%2*3,1,1,216);ht_limb(ht_scene,xx+2,top+62+n%2*3,xx+6,top+62+n%2*3,1,1,216);}
 ht_disk(ht_scene,x+87,top+19,3,230);ht_disk(ht_scene,x+87,top+19,1,40);
}
static unsigned ht_drawing_dog_open(const ht_drawing_state *s){return s->stage==3?(unsigned)ht_clamp((int)s->tick-32,0,64)*256/64:s->stage==4?256u:s->stage==5?256u-(unsigned)ht_min(s->tick,64)*256/64:0u;}
static ht_joint ht_drawing_dog_hand(const ht_drawing_state *s){
 ht_joint normal=ht_joint_at(427,194),bag=ht_joint_at(297,283);unsigned q=ht_drawing_dog_open(s);
 if(s->stage==3 && s->tick<32)return ht_joint_at(normal.x+(bag.x-normal.x)*s->tick/32,normal.y+(bag.y-normal.y)*s->tick/32);
 if(s->stage==3 || s->stage==4 || (s->stage==5 && s->tick<=64))return ht_joint_at(297,151+(256-(int)q)*70/256+62);
 if(s->stage==5){unsigned t=ht_min(s->tick-64,32);return ht_joint_at(bag.x+(normal.x-bag.x)*(int)t/32,bag.y+(normal.y-bag.y)*(int)t/32);}
 return normal;
}
static void ht_drawing_insert(const ht_drawing_state *s){
 int shift=s->stage==2?s->shift:0,left=35+shift,right=249;
 ht_rect(ht_scene,right,49,188,156,71);ht_drawing_picture(right+4,53,false);
 ht_rect(ht_scene,left,46,187,158,46);ht_drawing_picture(left+3,49,true);
 ht_limb(ht_scene,left,47,left+185,47,1,0,137);ht_limb(ht_scene,left,203,left+186,203,1,1,112);
 ht_limb(ht_scene,95,253,left+8,193,6,4,239);ht_disk(ht_scene,left+8,193,4,220);
 unsigned q=ht_drawing_dog_open(s);if(q)ht_drawing_dog(197,151,q);
 /* The right hand releases the wall card, takes the railway paper from below
  * frame and stays attached to its corner through placement and withdrawal. */
 ht_joint hand=ht_drawing_dog_hand(s);ht_limb(ht_scene,401,253,hand.x,hand.y,6,4,239);ht_disk(ht_scene,hand.x,hand.y,4,220);
}
static ht_person_pose ht_drawing_pose(const ht_game *game,const ht_drawing_state *s){
 ht_game v=*game;int target=ht_drawing_x()-2,wx=game->x/256;
 unsigned q=s->stage==1?ht_min(s->tick,32)*256/32:s->stage>=2?256:0;
 wx+=(target-wx)*(int)q/256;v.x=wx*256;v.y=ht_surface_at(&v,7,wx)*256;v.vx=s->stage==1 && s->tick<32?248:0;v.vy=0;v.facing=1;v.grounded=true;v.landing_ticks=0;v.stride=s->tick*96;memset(&v.traversal,0,sizeof(v.traversal));
 int x=wx-v.camera/256,f=v.y/256-v.camera_y/256;ht_person_pose p=ht_person_pose_at(x,f,&v);
 if(v.vx){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=x+(p.foot[i].x-x)/2;}
 if(s->stage && s->stage<8){
  unsigned reach=s->stage==1?ht_clamp((int)s->tick-32,0,32)*256/32:s->stage==7?256-ht_clamp((int)s->tick-64,0,32)*256/32:256;
  unsigned lift=s->stage==1?ht_clamp((int)s->tick-64,0,32)*256/32:s->stage==7?256-ht_min(s->tick,64)*256/64:256;
  for(int i=0;i<2;++i){ht_joint hold=ht_joint_at(ht_drawing_x()+(i?6:-6)-v.camera/256-(int)lift*2/256,ht_drawing_floor()-v.camera_y/256-15-(int)lift*3/256);p.hand[i].x+=(hold.x-p.hand[i].x)*(int)reach/256;p.hand[i].y+=(hold.y-p.hand[i].y)*(int)reach/256;}
 }
 return p;
}
static void ht_drawing_study_render(const ht_game *game,const ht_drawing_state *s){
 ht_game v=*game;v.camera=(ht_drawing_x()-239)*256;v.camera_y=(ht_drawing_floor()-192)*256;v.intimacy=0;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;v.drawing_view_stage=s->stage;v.drawing_view_tick=s->tick;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 if(s->stage>=2 && s->stage<=6){ht_world_scale=256;ht_drawing_insert(s);}else{ht_person_pose p=ht_drawing_pose(&v,s);ht_person_draw(&p);if((s->stage==1 && s->tick>=64) || (s->stage==7 && s->tick<64))ht_drawing_small_sheet(p.hand[0].x-6,p.hand[0].y-25,false);}
 ht_world_scale=256;ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_drawing_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A LIFT DRAWING  START NOTE  X RETURN":s->stage==2?"L/R COMPARE  A SET DOG BESIDE  X RETURN":s->stage==4?"A PUT DOG AWAY  START NOTE  X RETURN":s->stage==6?"A RETURN DRAWING  START NOTE  X RETURN":s->stage==8?"A READ NOTE  START NOTE  X RETURN":"PAUSE HOLD  START NOTE  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
