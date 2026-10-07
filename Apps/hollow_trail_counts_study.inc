#define HT_COUNTS_STUDY 1
typedef struct {bool active,paused;int8_t dx,dy,move_x,move_y;uint8_t stage;uint16_t tick;} ht_counts_state;
static ht_counts_state ht_counts;
static bool ht_counts_near(const ht_game *g){return g->level==6 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==1;}
static void ht_counts_begin(void){ht_counts=(ht_counts_state){true,false,34,-16,0,0,0,0};}
static bool ht_counts_aligned(const ht_counts_state *s){return !s->dx && !s->dy;}
static bool ht_counts_step(ht_counts_state *s){
 if(!s->active || s->paused)return false;
 if(s->stage==2){int x=s->dx,y=s->dy;s->dx=(int8_t)ht_clamp(x+s->move_x,-48,48);s->dy=(int8_t)ht_clamp(y+s->move_y,-24,24);return s->dx!=x || s->dy!=y;}
 if(s->stage!=1 && s->stage!=4)return false;
 if(++s->tick==160){++s->stage;s->tick=0;}return true;
}
static bool ht_counts_action(ht_counts_state *s){
 if(!s->active || s->paused || (s->stage!=0 && s->stage!=2 && s->stage!=3) || (s->stage==2 && !ht_counts_aligned(s)))return false;
 ++s->stage;s->tick=0;s->move_x=s->move_y=0;return true;
}
static const char *ht_counts_label(const ht_counts_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "TWO CONSECUTIVE MORNINGS SHOWED THE SAME CORRECTION.";
 if(s->stage==1)return "I CARRIED BOTH SHEETS TO THE LAST CLEAR PANE.";
 if(s->stage==2)return ht_counts_aligned(s)?"THE CORRECTIONS COVERED EACH OTHER EXACTLY.":"I HELD ONE SHEET OVER THE OTHER.";
 if(s->stage==3)return "MY FACE APPEARED BEHIND THE FIGURES.";
 return "I PRESSED THE COUNTS BENEATH AN EMPTY POT.";
}
/* One path is printed twice. The short fork, dot and unjoined gap are geometry,
 * not independently improvised lookalikes or a text claim of a match. */
static void ht_counts_correction(int x,int y,int ink){
 static const int8_t six[][2]={{-7,-22},{-16,-18},{-20,-7},{-20,6},{-15,13},{-7,12},{-3,5},{-5,-2},{-12,-5},{-19,-1}};
 for(unsigned n=0;n+1<sizeof(six)/sizeof(six[0]);++n)ht_limb(ht_scene,x+six[n][0],y+six[n][1],x+six[n+1][0],y+six[n+1][1],1,1,ink);
 ht_limb(ht_scene,x-25,y+9,x+1,y-16,1,1,ink); /* Six crossed out. */
 ht_limb(ht_scene,x+5,y-21,x+26,y-23,1,1,ink);ht_limb(ht_scene,x+26,y-23,x+11,y+10,1,1,ink);
 ht_limb(ht_scene,x+11,y+10,x+15,y+14,1,1,ink);ht_limb(ht_scene,x+15,y+14,x+21,y+12,1,1,ink);
 ht_limb(ht_scene,x+15,y+14,x+21,y+17,1,1,ink); /* Slight fork. */
 ht_disk(ht_scene,x+16,y+20,1,ink); /* Dot at the hook's base. */
 ht_limb(ht_scene,x+21,y+12,x+23,y+8,1,1,ink); /* Ends short: never join to the seven. */
}
static void ht_counts_reflection(int x,int y){
 /* Night makes a weak, ordinary reflected face behind the translucent pages. */
 ht_limb(ht_scene,x-15,y-21,x-21,y-5,2,2,91);ht_limb(ht_scene,x-21,y-5,x-15,y+20,2,2,88);
 ht_limb(ht_scene,x-15,y+20,x-3,y+29,2,2,86);ht_limb(ht_scene,x-3,y+29,x+11,y+20,2,2,84);
 ht_limb(ht_scene,x+11,y+20,x+17,y-7,2,2,86);ht_limb(ht_scene,x+17,y-7,x+10,y-22,2,2,91);
 ht_limb(ht_scene,x-15,y-21,x+10,y-22,3,3,94);
 ht_limb(ht_scene,x-12,y+1,x-7,y+1,1,1,88);ht_limb(ht_scene,x+4,y+1,x+9,y+1,1,1,88);
 ht_limb(ht_scene,x,y+3,x-2,y+12,1,0,88);ht_limb(ht_scene,x-5,y+18,x+4,y+18,1,0,86);
}
static void ht_counts_insert(const ht_counts_state *s){
 int dx=s->stage==3?0:s->dx,dy=s->stage==3?0:s->dy;
 /* Frame and surviving glass continue the world pane, rather than a UI card. */
 ht_rect(ht_scene,90,32,299,188,182);ht_rect(ht_scene,96,37,287,178,97);
 ht_limb(ht_scene,103,43,133,65,1,1,70);ht_limb(ht_scene,359,183,374,198,1,1,68);
 ht_rect(ht_scene,143,64,174,134,57);ht_rect(ht_scene,149+dx,68+dy,174,134,61);
 ht_counts_reflection(242,119);
 /* Rear paper remains visible through the front paper. Offset border sizes
  * let both sheets read even when their copied correction exactly coincides. */
 ht_limb(ht_scene,143,64,316,64,1,1,110);ht_limb(ht_scene,143,64,143,197,1,1,110);ht_limb(ht_scene,143,197,316,197,1,1,110);ht_limb(ht_scene,316,64,316,197,1,1,110);
 ht_limb(ht_scene,149+dx,68+dy,322+dx,68+dy,1,1,158);ht_limb(ht_scene,149+dx,68+dy,149+dx,201+dy,1,1,158);ht_limb(ht_scene,149+dx,201+dy,322+dx,201+dy,1,1,158);ht_limb(ht_scene,322+dx,68+dy,322+dx,201+dy,1,1,158);
 for(int n=0;n<3;++n){ht_limb(ht_scene,159,82+n*35,301,82+n*35,0,0,95);ht_limb(ht_scene,165+dx,86+dy+n*35,307+dx,86+dy+n*35,0,0,106);}
 ht_counts_correction(240,122,140);ht_counts_correction(240+dx,122+dy,229);
 /* Each wrist meets its own sheet, including vertical and horizontal movement. */
 ht_limb(ht_scene,107,246,146,183,6,4,237);ht_disk(ht_scene,146,183,4,219);
 ht_limb(ht_scene,368,246,319+dx,187+dy,6,4,237);ht_disk(ht_scene,319+dx,187+dy,4,219);
 if(s->stage==3){
  ht_rect(ht_scene,32,13,416,36,255);ht_text_color(33,16,"WRITE TOMORROW'S COUNT EVEN IF I AM GONE.",1,80);
  ht_text_color(33,28,"SOMEONE MUST KNOW WHAT IS STILL GROWING.",1,80);
  ht_text_color(160,207,"ASK WHO HELD THE PEN.",1,222);
 }
}
static ht_joint ht_counts_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_counts_pose(const ht_game *game,const ht_counts_state *s){
 ht_game v=*game;int wire=ht_counts_x()-5,pane=ht_counts_x()-105,wx=wire;unsigned t=s->tick;
 if(!s->stage)wx=game->x/256;
 else if(s->stage==1)wx=t<32?game->x/256+(wire-game->x/256)*(int)t/32:t<96?wire:wire+(pane-wire)*(int)(t-96)/64;
 else if(s->stage==2 || s->stage==3)wx=pane;
 else if(s->stage==4)wx=pane+(wire-pane)*(int)ht_min(t,96)/96;
 v.x=wx*256;v.y=ht_surface_at(&v,4,wx)*256;v.vx=(s->stage==1 && (t<32 || t>=96))?-248:s->stage==4 && t<96?248:0;v.vy=0;v.facing=s->stage==1?-1:1;v.grounded=true;v.landing_ticks=0;v.stride=t*80;memset(&v.traversal,0,sizeof(v.traversal));
 int x=wx-v.camera/256,f=v.y/256-v.camera_y/256;ht_person_pose p=ht_person_pose_at(x,f,&v);
 if(v.vx){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=x+(p.foot[i].x-x)/2;}
 if(s->stage==1 && t>=32){
  unsigned reach=ht_min(t-32,32)*256/32;
  for(int i=0;i<2;++i){ht_joint hold=ht_joint_at(p.shoulder.x+(i?7:-5),f-23+i*2);ht_joint clip=ht_joint_at(ht_counts_x()+(i?4:-10)-v.camera/256,ht_counts_floor()-v.camera_y/256-25);
   p.hand[i]=t<64?ht_counts_mix(p.hand[i],clip,reach):t<96?ht_counts_mix(clip,hold,(t-64)*256/32):hold;}
 }else if(s->stage==2 || s->stage==3 || s->stage==4){
  for(int i=0;i<2;++i)p.hand[i]=ht_joint_at(p.shoulder.x+(i?7:-5),f-23+i*2);
  if(s->stage==4 && t>=96){
   unsigned q=ht_min(t-96,32)*256/32;int crouch=(t<128?(int)q:256-(int)ht_min(t-128,32)*256/32);
   p.shoulder.y+=crouch*7/256;p.hip.y+=crouch*4/256;
   for(int i=0;i<2;++i){ht_joint contact=ht_joint_at(ht_counts_x()+(i?6:-3)-v.camera/256,ht_counts_floor()-v.camera_y/256-16-(i?4:0));p.hand[i]=ht_counts_mix(p.hand[i],contact,(unsigned)crouch);}
  }
 }
 return p;
}
static void ht_counts_study_render(const ht_game *game,const ht_counts_state *s){
 ht_game v=*game;v.camera=(ht_counts_x()-300)*256;v.camera_y=(ht_counts_floor()-192)*256;v.intimacy=0;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 v.counts_view_stage=s->stage;v.counts_view_tick=s->tick;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 if(s->stage==2 || s->stage==3){ht_world_scale=256;ht_counts_insert(s);}
 else{ht_person_pose p=ht_counts_pose(&v,s);ht_person_draw(&p);
  if(s->stage==1 && s->tick>=64)for(int i=0;i<2;++i)ht_counts_small_sheet(p.hand[i].x-(i?3:6),p.hand[i].y-2);
  if(s->stage==4 && s->tick<128){
   /* Carry the now-overlaid pair in one hand; the other lifts the pot. The
    * pages flatten onto the bench continuously instead of clipping through it. */
   int q=ht_clamp((int)s->tick-96,0,32),left=p.hand[0].x-6+q/8,top=p.hand[0].y-2+q/8;
   for(int i=0;i<2;++i){int xx=left+i*2,yy=top-i*2,w=12+q*10/32,h=13-q*11/32;
    ht_rect(ht_scene,xx,yy,w,h,45+i*10);if(h>5)ht_rect(ht_scene,xx+2,yy+3,w-4,1,132);}
  }}
 ht_world_scale=256;ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_counts_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A TAKE BOTH SHEETS  START NOTE  X RETURN":s->stage==2?"D-PAD ALIGN SHEET  A COMPARE  X RETURN":s->stage==3?"A PUT COUNTS BACK  START NOTE  X RETURN":s->stage==5?"A READ NOTE  START NOTE  X RETURN":"START NOTE  PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
