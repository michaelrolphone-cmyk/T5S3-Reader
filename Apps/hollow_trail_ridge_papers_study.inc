/* Fixed actions, deliberate holds. Inventory only changes after the biscuit
 * and crumbs finish; paper and scarf ownership never changes in this room. */
typedef struct {bool active,paused,dog,food;uint8_t stage;uint16_t tick;} ht_papers_state;
static ht_papers_state ht_papers;
static bool ht_papers_near(const ht_game *g){return g->level==8 && g->grounded && !g->traversal.mode && !g->door_stage && ht_evidence_near(g)==1;}
static void ht_papers_begin(void){ht_papers=(ht_papers_state){true,false,(ht.packet&HT_PACKET_DOG)!=0,(ht.packet&(HT_PACKET_FOOD|HT_PACKET_CRUMBS))==HT_PACKET_FOOD,0,0};}
static unsigned ht_papers_limit(unsigned stage){return !(stage&1) || stage>13?0:stage==7?64:stage==11?160:128;}
static bool ht_papers_step(ht_papers_state *s){
 if(!s->active || s->paused || !ht_papers_limit(s->stage))return false;
 if(++s->tick==ht_papers_limit(s->stage)){
  if(s->stage==11)ht.packet|=HT_PACKET_CRUMBS;
  ++s->stage;s->tick=0;
 }return true;
}
static bool ht_papers_action(ht_papers_state *s){
 if(!s->active || s->paused || (s->stage&1) || s->stage>=14)return false;
 s->stage+=(s->stage==4 && !s->dog) || (s->stage==10 && !s->food)?2:1;s->tick=0;return true;
}
static const char *ht_papers_label(const ht_papers_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "A FLAT STONE HELD PAPERS AGAINST A NARROW SHELF.";
 if(s->stage==1 || s->stage==2)return "THERE WAS NO OTHER WAY.";
 if(s->stage==3)return "I SAT ON THE FLOOR AND SPREAD THE PAPERS.";
 if(s->stage==4)return "THE DOORWAY STAYED EMPTY.";
 if(s->stage==5)return "I SMOOTHED THE SOFT CORNER AGAINST THE NOTEBOOK.";
 if(s->stage==6)return s->dog?"THE EXTRA LEG REMAINED. I READ HIS SENTENCE AGAIN.":"THEN I READ THE KEEPER'S SENTENCE AGAIN.";
 if(s->stage==7 || s->stage==8)return "AN OLD BURN, SHAPED LIKE THE BASE OF A LAMP.";
 if(s->stage==9)return "WATER REPLACED THE SCRAPING SOUND.";
 if(s->stage==10)return "LIGHT ENTERED THROUGH THE ROOF SEAMS.";
 if(s->stage==11)return s->tick<96?"I ATE ONE OF THE BISCUITS FROM THE GARDENS.":"I LEFT THE CRUMBS BY THE SMALL HOLE IN THE WALL.";
 if(s->stage==12)return s->food?"FOR WHATEVER HAD MADE THE SMALL HOLE.":"THE LIGHT WAS PALE AND UNCERTAIN.";
 return "WHEN I STEPPED OUT, THE HIGHER RIDGE WAS VISIBLE.";
}
static ht_joint ht_papers_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_papers_pose(const ht_game *g,const ht_papers_state *s){
 ht_game v=*g;int wx=ht_papers_x()-11;
 if(s->stage==1)wx=g->x/256+(wx-g->x/256)*(int)ht_min(s->tick,64)/64;
 else if(!s->stage)wx=g->x/256;
 else if(s->stage==14)wx=ht_papers_x()-83;
 else if(s->stage==13 && s->tick>=32)wx-=(int)ht_min(s->tick-32,96)*72/96;
 v.x=wx*256;v.y=ht_surface_at(&v,4,wx)*256;v.vx=v.vy=0;v.facing=1;v.grounded=true;v.landing_ticks=0;v.stride=0;memset(&v.traversal,0,sizeof(v.traversal));
 int cx=v.camera/256,cy=v.camera_y/256,f=ht_papers_floor()-cy;
 ht_person_pose p=ht_person_pose_at(wx-cx,v.y/256-cy,&v);
 if(s->stage==1 || s->stage==2){ht_joint c=ht_papers_stone();c.x-=cx;c.y-=cy;p.hand[1]=ht_papers_mix(p.hand[1],c,s->stage==2?256:(unsigned)ht_clamp((int)s->tick-64,0,64)*4);}
 if(s->stage>=3 && s->stage<=13){
  ht_person_pose sit=p;sit.hip=ht_joint_at(ht_papers_x()-13-cx,f-4);sit.shoulder=ht_joint_at(ht_papers_x()-15-cx,f-15);
  for(int i=0;i<2;++i){sit.foot[i]=ht_joint_at(ht_papers_x()-5+i*2-cx,ht_surface_at(g,4,ht_papers_x()-5+i*2)-cy);sit.hand[i]=ht_joint_at(ht_papers_x()-9+i*2-cx,f-3);}
  unsigned q=s->stage==3?ht_min(s->tick,64)*4:s->stage==13?256-ht_min(s->tick,32)*8:256;
  p.hip=ht_papers_mix(p.hip,sit.hip,q);p.shoulder=ht_papers_mix(p.shoulder,sit.shoulder,q);
  for(int i=0;i<2;++i){p.foot[i]=ht_papers_mix(p.foot[i],sit.foot[i],q);p.hand[i]=ht_papers_mix(p.hand[i],sit.hand[i],q);}
  if(s->stage==7 || s->stage==8){ht_joint c=ht_papers_burn();c.x-=cx;c.y-=cy;p.hand[1]=ht_papers_mix(p.hand[1],c,s->stage==8?256:ht_min(s->tick,64)*4);}
 }
 return p;
}
static ht_joint ht_papers_sheet(const ht_papers_state *s,unsigned n){
 ht_joint shelf=ht_joint_at(357+(int)n*3,57+(int)n*2),floor=ht_joint_at(53+(int)n*59,137+(int)(n%2)*23);
 return ht_papers_mix(shelf,floor,s->stage<3?0:s->stage==3?(unsigned)ht_clamp((int)s->tick-64,0,64)*4:256);
}
static ht_joint ht_papers_dog_corner(const ht_papers_state *s){return ht_joint_at(306,180-(s->stage==5?(128-(int)ht_min(s->tick,128))*12/128:0));}
static ht_joint ht_papers_burn_detail(void){return ht_joint_at(349,186);}
static ht_joint ht_papers_hand(const ht_papers_state *s){
 ht_joint rest=ht_joint_at(427,220),burn=ht_papers_burn_detail();
 if(s->stage==5 || s->stage==6){ht_joint c=ht_papers_dog_corner(s);return ht_papers_mix(rest,c,s->stage==6?256:ht_min(s->tick,48)*256/48);}
 if(s->stage==7 || s->stage==8)return ht_papers_mix(rest,burn,s->stage==8?256:ht_min(s->tick,64)*4);
 if(s->stage==11){unsigned t=ht_min(s->tick,160);ht_joint mouth=ht_joint_at(402,64),hole=ht_joint_at(91,190);return t<32?ht_papers_mix(rest,mouth,t*8):t<96?mouth:ht_papers_mix(mouth,hole,(t-96)*4);}
 if(s->stage==12 && s->food)return ht_joint_at(91,190);
 return rest;
}
static void ht_papers_insert(const ht_game *g,const ht_papers_state *s){
 ht_rect(ht_scene,0,0,480,226,155);
 for(int n=0;n<7;++n)ht_limb(ht_scene,0,89+n*21,480,95+n*21,1,1,177);
 ht_rect(ht_scene,12,0,42,112,66); /* The same empty door and its light. */
 ht_limb(ht_scene,306,82,463,82,5,4,218);ht_rect(ht_scene,338,57,93,23,55);
 if(s->stage<3){
  unsigned q=s->stage==1?ht_min(s->tick,128)*2:256;int x=377+(int)q*67/256,y=54+(int)q*14/256;
  ht_limb(ht_scene,x-14,y,x+13,y,5,3,207);ht_limb(ht_scene,479,154,x+6,y+3,8,4,230);
  ht_text_color(64,99,"THERE WAS NO OTHER WAY.",1,222);
  ht_text_color(64,123,"I BELIEVED SHE WAS REPEATING",1,216);ht_text_color(64,138,"WHAT SHE HAD BEEN TOLD.",1,216);
  ht_text_color(64,157,"THEN SHE CORRECTED THE ORDER",1,216);ht_text_color(64,172,"BEFORE I OPENED THE ENVELOPE.",1,216);
  ht_text_color(64,195,"I NO LONGER KNOW WHO GAVE IT.",1,216);
 }else{
  ht_limb(ht_scene,434,70,458,70,5,3,207);
  /* Two local sheets always exist. Earlier papers appear only if found;
   * the tracing stays nearest the door, with no invented extra finding. */
  static const uint8_t pages[5]={24,6,8,10,17};
  for(unsigned n=0;n<7;++n){if(n<5 && !ht_evidence_found(g,pages[n]))continue;ht_joint a=ht_papers_sheet(s,n);
   ht_rect(ht_scene,a.x,a.y,50,35,58+n%2*11);
   for(int row=0;row<4;++row){int slant=n==6?row*2:0;ht_limb(ht_scene,a.x+4,a.y+7+row*6,a.x+44-row%2*8,a.y+7+row*6+slant,0,0,183);}
  }
  if(s->stage==3){ht_joint a=ht_papers_sheet(s,5);ht_limb(ht_scene,409,225,a.x+45,a.y+30,8,4,229);}
  ht_joint b=ht_papers_burn_detail();ht_disk(ht_scene,b.x,b.y,21,237);ht_disk(ht_scene,b.x,b.y,16,173);
  ht_limb(ht_scene,b.x-17,b.y+10,b.x+15,b.y+10,2,2,217);
  ht_disk(ht_scene,78,192,9,242);ht_limb(ht_scene,68,201,90,201,2,1,197);
  if((s->stage==5 || s->stage==6) && s->dog){
   ht_rect(ht_scene,191,112,123,88,212);ht_rect(ht_scene,197,117,113,78,50);
   ht_drawing_dog(201,121,256);ht_joint c=ht_papers_dog_corner(s);
   /* Softened corner unfolds against this notebook; the five drawn legs
    * and pierced eye remain the original railway drawing. */
   ht_triangle(ht_scene,c.x-15,c.y-3,c.x,c.y-15,c.x,c.y+10,99);
   ht_limb(ht_scene,c.x-15,c.y-3,c.x,c.y+10,1,0,146);
  }
  ht_joint h=ht_papers_hand(s);
  if(s->stage>=5 && s->stage<=12){ht_limb(ht_scene,474,226,h.x+22,h.y+21,13,7,235);ht_limb(ht_scene,h.x+22,h.y+21,h.x,h.y,6,3,213);ht_disk(ht_scene,h.x,h.y,4,214);}
  if(s->stage==11 && s->tick<96){ht_disk(ht_scene,h.x-5,h.y,6,103);ht_disk(ht_scene,402,41,18,203);ht_limb(ht_scene,390,63,403,66,1,1,232);}
  if((s->stage==11 && s->tick>=128) || (s->stage==12 && s->food) || (g->packet&HT_PACKET_CRUMBS))for(int n=0;n<6;++n)ht_rect(ht_scene,91+n*5,196+n%3*2,2,1,62);
 }
 if(s->stage>=9){unsigned light=s->stage==9?ht_min(s->tick,128)*2:256;ht_triangle(ht_scene,167,0,171,0,235,166,155-(int)light*72/256);ht_triangle(ht_scene,337,0,340,0,403,134,155-(int)light*55/256);}
 /* A bounded melt at the door; it never advances the live weather cue. */
 if(s->stage>=3 && s->stage<9){unsigned melt=s->stage&1?s->tick%64:32;int y=108+(int)ht_min(melt,32)*2;ht_rect(ht_scene,35,y,2,melt<32?3:1,melt<32?31:102);}
 ht_checkpoint();
}
static void ht_papers_study_render(const ht_game *game,const ht_papers_state *s){
 ht_game v=*game;v.camera=(ht_papers_x()-270)*256;v.camera_y=(ht_papers_floor()-191)*256;
 v.intimacy=256;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);
 ht_world_scale=ht_scene_scale(&v);ht_person_pose p=ht_papers_pose(&v,s);ht_person_draw(&p);ht_person_scarf(&p,game->scarf);
 ht_world_scale=256;if(s->stage>=2 && s->stage<=12)ht_papers_insert(game,s);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_papers_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A LIFT STONE  START NOTE  X RETURN":s->stage==2?"A SIT WITH PAPERS  START NOTE  X RETURN":s->stage==4?(s->dog?"A SMOOTH DOG DRAWING  START NOTE  X RETURN":"A READ AGAIN  START NOTE  X RETURN"):s->stage==6?"A TOUCH OLD BURN  START NOTE  X RETURN":s->stage==8?"A WAIT FOR SLEET  START NOTE  X RETURN":s->stage==10?(s->food?"A EAT BISCUIT  START NOTE  X RETURN":"A LOOK AT DOOR  START NOTE  X RETURN"):s->stage==12?"A STEP OUT  START NOTE  X RETURN":s->stage==14?"A READ NOTE  START NOTE  X RETURN":"PAUSE HOLD  START NOTE  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
