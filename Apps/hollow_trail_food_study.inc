#define HT_FOOD_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick,ambient;} ht_food_state;
static ht_food_state ht_food;
static bool ht_food_near(const ht_game *g){
 /* Do not stage a body through a crate the player left beside the bench.
  * The original X330 crate is clear; no movement or progress flag is changed. */
 return g->level==6 && g->grounded && !g->traversal.mode && ht_abs(g->x/256-HT_FOOD_X)<=7 &&
  ht_abs(g->y/256-ht_surface_at(g,0,g->x/256))<3 && (g->traversal.crate_x<(HT_FOOD_X-25)*256 || g->traversal.crate_x>481*256);
}
static void ht_food_begin(void){ht_food=(ht_food_state){true,false,0,0,0};}
static unsigned ht_food_limit(unsigned stage){
 return stage==1?192:stage==4?128:stage==2?96:stage==6 || stage==8?160:stage==13?320:stage==10?256:0;
}
static bool ht_food_step(ht_food_state *s){
 if(!s->active || s->paused)return false;
 unsigned limit=ht_food_limit(s->stage);bool changed=false;
 if(s->stage<6){s->ambient=(s->ambient+1)&255u;changed=s->ambient==0 || s->ambient>=240;}
 if(!limit)return changed;
 if(++s->tick==limit){if(s->stage==13)s->active=false;else{++s->stage;s->tick=0;}}
 return true;
}
static bool ht_food_action(ht_food_state *s){
 if(!s->active || s->paused || ht_food_limit(s->stage))return false;
 ++s->stage;s->tick=0;return true;
}
static const char *ht_food_label(const ht_food_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "THE THICK RETURN SWEATED IN THE COOLER AIR.";
 if(s->stage==1)return "I DRANK.";
 if(s->stage==2 || s->stage==3)return "WAITED.";
 if(s->stage==4 || s->stage==5)return "DRANK AGAIN.";
 if(s->stage==6 || s->stage==7)return "THERE WAS FOOD IN A CUPBOARD UNDER THE WORKBENCH.";
 if(s->stage==8 || s->stage==9)return "I HELD ONE BISCUIT BETWEEN BOTH HANDS.";
 if(s->stage==10)return s->tick<64?"THE FIRST BITE HURT MY DRY MOUTH.":s->tick<128?"I ATE TOO QUICKLY ANYWAY.":"I STOOD STILL UNTIL THE CRAMP EASED.";
 if(s->stage==11)return "THE CUPBOARD DOOR HAD A LIST ON THE INSIDE.";
 if(s->stage==12)return "MY SISTER'S WRITING RAN ACROSS THE BOTTOM.";
 return "I SHUT THE DOOR GENTLY.";
}
static ht_joint ht_food_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_food_pose(const ht_game *game,const ht_food_state *s){
 ht_game v=*game;unsigned t=s->tick;int wx=HT_FOOD_X,f=ht_food_floor();
 if(s->stage==1)wx+=12*ht_clamp((int)t-32,0,32)/32;
 if(s->stage>=2 && s->stage<=5)wx+=12;
 if(s->stage>=7)wx=HT_FOOD_CUPBOARD_X;
 else if(s->stage==6)wx=t<32?HT_FOOD_X+12-(int)t*12/32:t<96?HT_FOOD_X+83*ht_clamp((int)t-32,0,64)/64:456-30*(int)ht_min(t-96,64)/64;
 if(s->stage==13)wx=t<128?HT_FOOD_CUPBOARD_X+30*(int)t/128:456+(game->x/256-456)*(int)ht_min(t-128,128)/128;
 v.x=wx*256;v.y=f*256;v.vx=s->stage==6 && t>=32 && t<96?232:s->stage==13 && t>=128 && t<256?-232:0;v.vy=0;v.facing=s->stage==13 && t>=128?-1:1;v.grounded=true;v.landing_ticks=0;v.stride=t*92;memset(&v.traversal,0,sizeof(v.traversal));
 int x=wx-v.camera/256,y=f-v.camera_y/256;ht_person_pose p=ht_person_pose_at(x,y,&v);
 if(v.vx)for(int i=0;i<2;++i)p.foot[i].x=x+(p.foot[i].x-x)*3/4;
 ht_joint cup=ht_joint_at(HT_FOOD_X-v.camera/256,y-29),hold=ht_joint_at(x+7,y-18),mouth=ht_joint_at(x+4,y-26);
 if(s->stage==1){
  ht_joint water=ht_joint_at(HT_FOOD_X+20-v.camera/256,y-26);
  p.hand[0]=t<32?ht_food_mix(p.hand[0],cup,t*256/32):t<64?ht_food_mix(cup,water,(t-32)*256/32):t<96?water:t<128?ht_food_mix(water,mouth,(t-96)*256/32):t<160?mouth:ht_food_mix(mouth,hold,(t-160)*256/32);
 }
 else if(s->stage==2 || s->stage==3 || s->stage==5)p.hand[0]=hold;
 else if(s->stage==4)p.hand[0]=t<32?ht_food_mix(hold,mouth,t*256/32):t<96?mouth:ht_food_mix(mouth,hold,(t-96)*256/32);
 else if(s->stage==6 && t<32)p.hand[0]=ht_food_mix(hold,cup,t*256/32);
 if(s->stage==6 && t>=96){
  unsigned open=ht_food_open(s->stage,t),q=ht_min(t-96,16)*256/16;p.shoulder.y+=(int)q*4/256;
  ht_joint contact=ht_joint_at(HT_FOOD_X+90-(int)open*42/256-v.camera/256,y-13);
  p.hand[0]=ht_food_mix(p.hand[0],contact,q);
 }
 if(s->stage==8 || s->stage==9 || (s->stage==10 && t<128)){
  unsigned take=s->stage==8?ht_min(t,64)*256/64:256;
  unsigned lift=s->stage==8?ht_clamp((int)t-64,0,64)*256/64:256;
  if(s->stage==8){p.shoulder.y+=take*8/256*(256-(int)lift)/256;p.hip.y+=take*4/256*(256-(int)lift)/256;}
  ht_joint biscuit=ht_joint_at(x+6,y-20);
  if(s->stage==8)biscuit=ht_food_mix(ht_joint_at(x+9,y-10),biscuit,lift);
  if(s->stage==10){unsigned bite=t<32?t*256/32:t<96?256:(128-t)*256/32;biscuit=ht_food_mix(biscuit,mouth,bite);}
  for(int i=0;i<2;++i)p.hand[i]=ht_food_mix(p.hand[i],ht_joint_at(biscuit.x+(i?-2:1),biscuit.y),take);
 }else if(s->stage==10){
  unsigned cramp=t<192?256:256-(t-192)*256/64;p.shoulder.x+=(int)cramp*3/256;p.shoulder.y+=(int)cramp*3/256;
  p.hand[0]=ht_food_mix(p.hand[0],ht_joint_at(x+3,y-14),cramp);
 }
 if(s->stage==13 && t<128){unsigned open=ht_food_open(s->stage,t);p.shoulder.y+=4;p.hand[0]=ht_joint_at(HT_FOOD_X+90-(int)open*42/256-v.camera/256,y-13);}
 if(s->stage==13 && t>=256){
  ht_person_pose end=ht_person_pose_at((game->x-game->camera)/256,(game->y-game->camera_y)/256,game);
  unsigned q=ht_min(t-256,64)*256/64;p.hip=ht_food_mix(p.hip,end.hip,q);p.shoulder=ht_food_mix(p.shoulder,end.shoulder,q);
  for(int i=0;i<2;++i){p.hand[i]=ht_food_mix(p.hand[i],end.hand[i],q);p.foot[i]=ht_food_mix(p.foot[i],end.foot[i],q);}
 }
 return p;
}
static void ht_food_insert(bool note){
 /* Close view remains the open cupboard: food to the right, list on the
  * inside face at left. Rows imply portions/days/people without invented dates. */
 ht_rect(ht_scene,50,39,380,170,213);ht_rect(ht_scene,56,46,368,157,154);
 ht_rect(ht_scene,229,51,189,145,53);ht_rect(ht_scene,44,32,397,10,193);
 ht_rect(ht_scene,56,46,168,157,128);ht_rect(ht_scene,66,55,148,127,29);
 ht_text_color(73,63,"PORTIONS  DAYS  PEOPLE",1,166);
 for(int n=0;n<4;++n){int yy=82+n*12;ht_limb(ht_scene,74,yy,204,yy,0,0,92);for(int c=0;c<3;++c){int xx=82+c*45;ht_limb(ht_scene,xx,yy-6,xx+5,yy-4,1,0,150);ht_limb(ht_scene,xx+5,yy-4,xx+2,yy-1,0,0,150);}}
 if(note){ht_text_color(74,140,"KEEP ENOUGH TO",1,216);ht_text_color(74,153,"BEGIN AGAIN.",1,216);}
 else {ht_limb(ht_scene,74,147,203,147,1,0,151);ht_limb(ht_scene,74,156,170,156,1,0,151);}
 /* Sealed bean jar with glass shoulders and still-fast lid. */
 ht_rect(ht_scene,298,85,43,67,131);ht_rect(ht_scene,302,77,35,8,203);ht_limb(ht_scene,302,77,337,77,1,1,64);
 for(int n=0;n<21;++n){int xx=304+(n%4)*9,yy=99+(n/4)*9;ht_limb(ht_scene,xx,yy,xx+2,yy+3,2,2,193);ht_rect(ht_scene,xx+1,yy+1,1,2,88);}
 ht_limb(ht_scene,300,91,300,146,1,1,64);ht_rect(ht_scene,243,135,45,9,128);if(!note)ht_rect(ht_scene,249,124,38,8,142);
 for(int n=0;n<8;++n)if(n>=4 || !note)ht_rect(ht_scene,253+n%4*9,127+n/4*11,2,2,194);
 ht_rect(ht_scene,353,147,53,29,122);ht_rect(ht_scene,358,151,43,14,89);
 for(int n=0;n<4;++n){ht_limb(ht_scene,365+n*9,129+n%2*4,368+n*7,160,3,1,177);ht_limb(ht_scene,365+n*9,129+n%2*4,361+n*9,121,1,0,203);}
 ht_rect(ht_scene,234,178,180,5,186);
 if(!note){ht_text_color(286,57,"SEALED BEANS",1,192);ht_text_color(264,186,"BISCUITS / ROOTS IN SAND",1,218);}
 ht_limb(ht_scene,198,249,206,198,6,4,238);ht_disk(ht_scene,206,198,4,225);
}
static void ht_food_water_insert(const ht_food_state *s){
 /* The wait is close enough to see one falling drop and the deposit it made.
  * The same barrel rim, basin, tap and sweating return fill this held view. */
 ht_rect(ht_scene,0,0,480,226,82);ht_rect(ht_scene,0,212,480,14,223);
 ht_triangle(ht_scene,0,0,156,0,47,212,48);ht_triangle(ht_scene,155,0,218,0,126,212,64);
 ht_limb(ht_scene,33,212,39,62,2,2,135);ht_limb(ht_scene,39,62,63,19,2,2,135);ht_limb(ht_scene,63,19,93,-3,2,2,135);
 ht_limb(ht_scene,397,212,394,44,2,2,134);ht_limb(ht_scene,394,44,375,12,2,2,134);
 ht_limb(ht_scene,7,211,22,177,2,1,174);ht_triangle(ht_scene,18,187,2,164,27,179,153);
 ht_limb(ht_scene,350,64,350,218,12,12,192);ht_limb(ht_scene,350,218,380,231,12,12,192);
 for(int n=0;n<6;++n){ht_limb(ht_scene,340,91+n*5,359,94+n*5,1,1,133);ht_rect(ht_scene,342+n%2*13,134+n*11,2,4,43);}
 ht_limb(ht_scene,349,67,277,67,6,6,198);ht_limb(ht_scene,277,67,277,82,4,4,198);ht_limb(ht_scene,301,53,319,53,3,3,191);ht_limb(ht_scene,310,53,310,64,2,2,192);
 ht_triangle(ht_scene,147,164,335,164,321,210,187);ht_triangle(ht_scene,147,164,321,210,161,210,187);
 for(int n=0;n<8;++n)ht_limb(ht_scene,158+n*23,173,166+n*21,209,1,1,214);
 ht_limb(ht_scene,158,196,327,196,3,3,229);
 ht_limb(ht_scene,145,162,334,162,4,4,203);ht_limb(ht_scene,153,159,327,159,2,2,58);
 ht_triangle(ht_scene,259,155,277,135,298,155,87);ht_triangle(ht_scene,267,146,277,135,282,147,38);
 for(int n=0;n<8;++n)ht_rect(ht_scene,268+(int)(ht_hash(n+907)%19),143+(int)(ht_hash(n+134)%11),2,1,133);
 if(s->ambient>=240)ht_limb(ht_scene,277,88+(int)(s->ambient-240)*43/16,277,91+(int)(s->ambient-240)*43/16,1,1,49);
 /* Held cup rests between the hands, away from the slow filling basin. */
 ht_limb(ht_scene,69,231,108,196,8,5,235);ht_limb(ht_scene,161,235,140,195,8,5,233);
 ht_rect(ht_scene,107,164,34,34,76);ht_limb(ht_scene,106,164,141,164,2,2,199);ht_limb(ht_scene,142,170,151,173,2,2,194);ht_limb(ht_scene,151,173,149,186,2,2,194);ht_limb(ht_scene,149,186,142,190,2,2,194);
}
static void ht_food_study_render(const ht_game *game,const ht_food_state *s){
 ht_game v=*game;v.camera=(HT_FOOD_X+35-240)*256;v.camera_y=(ht_food_floor()-172)*256;v.intimacy=512;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.food_view_active=true;v.food_view_stage=s->stage;v.food_view_tick=s->tick;v.food_view_ambient=s->ambient;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 ht_person_pose p=ht_food_pose(&v,s);
 if(s->stage!=2 && s->stage!=3 && s->stage!=7 && s->stage!=12){ht_person_draw(&p);
  if((s->stage==1 && s->tick>=32) || (s->stage>=2 && s->stage<=5) || (s->stage==6 && s->tick<32))ht_food_cup(p.hand[0].x,p.hand[0].y);
  if((s->stage==8 && s->tick>=64) || s->stage==9 || (s->stage==10 && s->tick<96)){ht_rect(ht_scene,p.hand[0].x-3,p.hand[0].y-2,s->stage==10 && s->tick>=32?3:6,3,113);}
 }
 ht_world_scale=256;if(s->stage==2 || s->stage==3)ht_food_water_insert(s);
 if(s->stage==7 || s->stage==12)ht_food_insert(s->stage==12);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_food_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A DRINK  START NOTES  X RETURN":s->stage==3?"A DRINK AGAIN  START NOTES  X RETURN":s->stage==5?"A OPEN CUPBOARD  START NOTES  X RETURN":s->stage==7?"A TAKE ONE BISCUIT  START NOTES  X RETURN":s->stage==9?"A EAT  START NOTES  X RETURN":s->stage==11?"A READ THE LIST  START NOTES  X RETURN":s->stage==12?"A CLOSE GENTLY  START NOTES  X RETURN":"PAUSE HOLD  START NOTES  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_food_prompt(const ht_game *g){if(ht_food_near(g)){ht_rect(ht_scene,133,221,214,15,255);ht_text_color(144,225,"A WATER AND FOOD BY THE WARM RETURN",1,85);}}
