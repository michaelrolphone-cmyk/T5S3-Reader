#define HT_CARE_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick,ambient;} ht_care_state;
static ht_care_state ht_care;
static bool ht_care_near(const ht_game *g){return g->level==6 && g->grounded && !g->traversal.mode && ht_abs(g->x/256-HT_CARE_X)<=7 && ht_abs(g->y/256-ht_surface_at(g,4,g->x/256))<3;}
static void ht_care_begin(void){ht_care=(ht_care_state){true,false,0,0,0};}
static unsigned ht_care_limit(unsigned stage){return stage==1 || stage==3?160:stage==5?256:stage==7?192:stage==9?96:0;}
static bool ht_care_step(ht_care_state *s){
 if(!s->active || s->paused)return false;
 /* One bounded drip cycle. Quiet holds do not consume story or evidence time. */
 s->ambient=(s->ambient+1)&127u;bool changed=s->ambient==0 || s->ambient>=112;
 unsigned limit=ht_care_limit(s->stage);if(!limit)return changed;
 if(++s->tick==limit){if(s->stage==9)s->active=false;else{++s->stage;s->tick=0;}}return true;
}
static bool ht_care_action(ht_care_state *s){if(!s->active || s->paused || ht_care_limit(s->stage))return false;++s->stage;s->tick=0;return true;}
static const char *ht_care_label(const ht_care_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->stage<2)return "THAT EVENING I MOVED A WATERLOGGED TRAY OUT OF A DRIP.";
 if(s->stage<4)return "AND SHIMMED IT LEVEL WITH SCRAPS OF WOOD.";
 if(s->stage<6)return "I TIED A STEM WITH THREAD FROM MY CUFF.";
 if(s->stage<8)return "THEN TURNED SEEDLINGS LEANING TOO FAR TOWARD THE LIGHT.";
 return s->stage==8?"THE TASKS QUIETED ME.":"I FOUND THE COUNT WHILE I WAS TURNING THE TRAYS.";
}
static ht_joint ht_care_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_care_pose(const ht_game *game,const ht_care_state *s){
 ht_game v=*game;unsigned t=s->tick,stage=s->stage;ht_care_geometry a=ht_care_layout(stage,t);
 int wx=game->x/256,kneel=0;
 if(stage==1){wx=t<32?game->x/256+(HT_CARE_TRAY_X-game->x/256)*(int)t/32:a.x;kneel=ht_min(t,32)*256/32;}
 else if(stage==2 || stage==3){wx=HT_CARE_DRY_X;kneel=stage==3?256-ht_clamp((int)t-128,0,32)*256/32:256;}
 else if(stage==4)wx=HT_CARE_DRY_X;
 else if(stage==5){wx=HT_CARE_DRY_X+(1595-HT_CARE_DRY_X)*(int)ht_min(t,64)/64;kneel=t<80?ht_clamp((int)t-64,0,16)*256/16:t<224?256:256-(int)ht_min(t-224,32)*256/32;}
 else if(stage==6)wx=1595;
 else if(stage==7){wx=1595+(HT_CARE_SEED_X-1595)*(int)ht_min(t,32)/32;kneel=t<64?ht_clamp((int)t-32,0,32)*256/32:t<160?256:256-(int)ht_min(t-160,32)*256/32;}
 else if(stage==8)wx=HT_CARE_SEED_X;
 else if(stage==9)wx=HT_CARE_SEED_X+(game->x/256-HT_CARE_SEED_X)*(int)ht_min(t,64)/64;
 bool walking=(stage==1 && (t<32 || (t>=64 && t<128))) || (stage==5 && t<64) || (stage==7 && t<32) || (stage==9 && t<64);
 int f=ht_care_floor(wx),cx=game->camera/256,cy=game->camera_y/256,x=wx-cx,y=f-cy;
 v.x=wx*256;v.y=f*256;v.vx=walking?(stage==1 || stage==9?-224:224):0;v.vy=0;v.facing=stage==1 || stage==9?-1:1;v.grounded=true;v.landing_ticks=0;v.stride=t*70;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(x,y,&v);
 if(walking){
  for(int i=0;i<2;++i){int lift=ht_person_ground(&v,p.foot[i].x,y)-p.foot[i].y;p.foot[i].x=x+(p.foot[i].x-x)/2;p.foot[i].y=ht_person_ground(&v,p.foot[i].x,y)-lift;}
  p.hip.y+=1;p.shoulder.y+=1;
 }
 p.hip=ht_care_mix(p.hip,ht_joint_at(x,y-7),(unsigned)kneel);
 p.shoulder=ht_care_mix(p.shoulder,ht_joint_at(x+2,y-12),(unsigned)kneel);
 for(int i=0;i<2;++i)p.hand[i]=ht_care_mix(p.hand[i],ht_joint_at(x+(i?5:-3),y-6),(unsigned)kneel);
 if(stage==1 || stage==2){unsigned grip=stage==2?256:ht_min(t,32)*256/32;
  for(int i=0;i<2;++i){int offset=i?8:-8,yy=a.left+(a.right-a.left)*(offset+10)/20;p.hand[i]=ht_care_mix(p.hand[i],ht_joint_at(a.x+offset-cx,yy-cy),grip);}
 }
 if(stage==3){
  unsigned reach=ht_min(t,32)*256/32,release=256-ht_clamp((int)t-112,0,16)*256/16;
  p.shoulder.x+=kneel*2/256;p.shoulder.y+=kneel*2/256;
  ht_joint scrap=ht_joint_at(HT_CARE_DRY_X+10-cx,ht_care_floor(HT_CARE_DRY_X+10)-cy-1);
  ht_joint wedge=ht_joint_at(HT_CARE_DRY_X+9-cx,ht_care_floor(HT_CARE_DRY_X)-cy-1);
  ht_joint work=ht_care_mix(scrap,wedge,(unsigned)ht_clamp((int)t-48,0,16)*256/16);
  p.hand[1]=ht_care_mix(p.hand[1],work,reach*release/256);
  p.hand[0]=ht_care_mix(p.hand[0],ht_joint_at(a.x-cx,a.left-cy),release);
 }
 if(stage==5 && t>=80){
  ht_joint cuff=ht_joint_at(x+1,y-17),pull=ht_joint_at(x+8,y-17);
  int sf=ht_care_floor(HT_CARE_STEM_X);ht_joint stem=ht_joint_at(HT_CARE_STEM_X-11+a.stem*11/256-cx,sf-6-a.stem*10/256-cy);
  ht_joint knot=ht_joint_at(HT_CARE_STEM_X+1-cx,sf-14-cy);
  if(t<112){unsigned q=ht_min(t-80,16)*256/16;p.hand[0]=ht_care_mix(p.hand[0],cuff,q);p.hand[1]=ht_care_mix(cuff,pull,q);}
  else if(t<144){p.hand[0]=ht_care_mix(cuff,stem,(t-112)*256/32);p.hand[1]=pull;}
  else if(t<176){p.hand[0]=stem;p.hand[1]=ht_care_mix(pull,knot,(t-144)*256/32);}
  else {unsigned q=t<224?256:256-(int)ht_min(t-224,32)*256/32;p.hand[0]=ht_care_mix(p.hand[0],ht_joint_at(knot.x-2,knot.y),q);p.hand[1]=ht_care_mix(p.hand[1],ht_joint_at(knot.x+2,knot.y+(t<208?(int)(t/8)%2:0)),q);}
 }
 if(stage==7 && t>=32){unsigned grip=t<64?(t-32)*256/32:t<160?256u:256u-(unsigned)ht_min(t-160,32)*256/32;
  for(int i=0;i<2;++i)p.hand[i]=ht_care_mix(p.hand[i],ht_joint_at(HT_CARE_SEED_X+(i?8:-8)-cx,ht_care_floor(HT_CARE_SEED_X)-6-cy),grip);
 }
 if(stage==9 && t>=64){ht_person_pose end=ht_person_pose_at((game->x-game->camera)/256,(game->y-game->camera_y)/256,game);unsigned q=ht_min(t-64,32)*256/32;p.hip=ht_care_mix(p.hip,end.hip,q);p.shoulder=ht_care_mix(p.shoulder,end.shoulder,q);for(int i=0;i<2;++i){p.hand[i]=ht_care_mix(p.hand[i],end.hand[i],q);p.foot[i]=ht_care_mix(p.foot[i],end.foot[i],q);}}
 return p;
}
/* Held macro view of the same cuff, rooted stem and existing cane. The lift
 * fraction is shared with the world geometry, never a second success state. */
static void ht_care_thread_insert(const ht_care_state *s){
 ht_care_geometry a=ht_care_layout(s->stage,s->tick);unsigned t=s->tick;
 ht_rect(ht_scene,0,0,480,226,99);ht_rect(ht_scene,0,202,480,24,210);
 ht_limb(ht_scene,343,0,343,204,4,4,185);ht_limb(ht_scene,352,0,352,204,1,1,55);
 for(int n=0;n<32;++n){int xx=25+(int)(ht_hash(n+623)%420),yy=207+(int)(ht_hash(n+719)%18);ht_rect(ht_scene,xx,yy,3,1,161);}
 ht_limb(ht_scene,282,211,282,74,3,3,163);ht_limb(ht_scene,285,208,285,75,1,1,197);
 int tx=214+a.stem*62/256,ty=193-a.stem*70/256;
 ht_limb(ht_scene,275,211,tx,ty,2,2,211);ht_triangle(ht_scene,tx,ty,tx-22,ty-13,tx-9,ty-1,163);ht_triangle(ht_scene,tx,ty,tx+16,ty-18,tx+10,ty-3,150);
 ht_joint cuff=ht_joint_at(132,155),pull=ht_joint_at(175,154),tip=ht_joint_at(tx,ty),knot=ht_joint_at(278,155),left=cuff,right=pull;
 if(t<112)right=ht_care_mix(cuff,pull,(unsigned)ht_clamp((int)t-80,0,16)*256/16);
 else if(t<144)left=ht_care_mix(cuff,tip,(t-112)*256/32);
 else if(t<176){left=tip;right=ht_care_mix(pull,knot,(t-144)*256/32);}
 else {left=ht_joint_at(knot.x-6,knot.y);right=ht_joint_at(knot.x+8,knot.y+(int)(t/8)%3-1);}
 /* Sleeves terminate in distinct clean cuffs; thread visibly leaves the left
  * cuff before it moves between the fingers and around the living stem. */
 ht_limb(ht_scene,70,235,left.x-7,left.y+14,13,8,235);ht_limb(ht_scene,left.x-9,left.y+15,left.x+1,left.y+8,3,3,102);
 ht_limb(ht_scene,left.x-3,left.y+9,left.x,left.y,5,3,219);ht_limb(ht_scene,left.x,left.y,left.x+4,left.y-2,2,1,220);
 ht_limb(ht_scene,385,237,right.x+9,right.y+15,12,8,235);ht_limb(ht_scene,right.x+3,right.y+9,right.x+13,right.y+17,3,3,110);
 ht_limb(ht_scene,right.x+7,right.y+11,right.x,right.y,5,3,219);ht_limb(ht_scene,right.x,right.y,right.x-4,right.y-1,2,1,220);
 if(t<176){ht_limb(ht_scene,left.x,left.y,right.x,right.y+4,1,1,43);if(t<112)ht_limb(ht_scene,left.x-3,left.y+10,left.x,left.y,1,1,43);}
 else {ht_limb(ht_scene,271,156,289,152,1,1,47);ht_limb(ht_scene,289,152,272,150,1,1,51);ht_limb(ht_scene,271,156,266,163,1,0,68);}
}
static void ht_care_study_render(const ht_game *game,const ht_care_state *s){
 ht_game v=*game;v.camera=(1581-240)*256;v.camera_y=(ht_care_floor(1581)-167)*256;v.intimacy=512;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.care_view_active=true;v.care_view_stage=s->stage;v.care_view_tick=s->tick;v.care_view_ambient=s->ambient;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 ht_person_pose p=ht_care_pose(&v,s);ht_person_draw(&p);
 if(s->stage==5 && s->tick>=80 && s->tick<208){ht_limb(ht_scene,p.hand[0].x,p.hand[0].y,p.hand[1].x,p.hand[1].y,0,0,45);if(s->tick<112)ht_limb(ht_scene,p.hand[0].x-1,p.hand[0].y-2,p.hand[0].x+2,p.hand[0].y,1,1,103);}
 if(s->stage==3 && s->tick>=48 && s->tick<64)ht_limb(ht_scene,p.hand[1].x-3,p.hand[1].y,p.hand[1].x+3,p.hand[1].y-1,1,1,166);
 ht_world_scale=256;if(s->stage==5 && s->tick>=80 && s->tick<224)ht_care_thread_insert(s);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_care_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A MOVE TRAY  START NOTES  X RETURN":s->stage==2?"A SHIM LEVEL  START NOTES  X RETURN":s->stage==4?"A TAKE CUFF THREAD  START NOTES  X RETURN":s->stage==6?"A TURN SEEDLINGS  START NOTES  X RETURN":s->stage==8?"A RETURN TO ROWS  START NOTES  X RETURN":"PAUSE HOLD  START NOTES  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_care_prompt(const ht_game *g){if(ht_care_near(g)){ht_rect(ht_scene,144,221,192,15,255);ht_text_color(156,225,"A TEND THE SEEDLINGS",1,85);}}
