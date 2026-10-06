/* Chapter VI: hold the repaired grip before reading the existing notice.
 * No study action writes puzzle, route, read-credit or traversal state. */
#define HT_GRIP_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_grip_state;
static ht_grip_state ht_grip;
static bool ht_grip_near(const ht_game *g){return g->level==5 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==1;}
static void ht_grip_begin(void){ht_grip=(ht_grip_state){true,false,0,0};}
static unsigned ht_grip_limit(unsigned stage){return stage==1 || stage==3 || stage==5?96:stage==7?192:0;}
static bool ht_grip_step(ht_grip_state *s){
 if(!s->active || s->paused || !ht_grip_limit(s->stage))return false;
 if(++s->tick==ht_grip_limit(s->stage)){++s->stage;s->tick=0;}return true;
}
static bool ht_grip_action(ht_grip_state *s){if(!s->active || s->paused || (s->stage&1) || s->stage>=8)return false;++s->stage;s->tick=0;return true;}
static bool ht_grip_tucked(const ht_grip_state *s){return s->stage>=4 || (s->stage==3 && s->tick>=48);}
static const char *ht_grip_label(const ht_grip_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "THE SPLIT HANDLE WAS BOUND IN MY SISTER'S SCARF.";
 if(s->stage==1)return "I PUT MY HAND AROUND THE HANDLE.";
 if(s->stage==2)return "THE BROKEN WOOD BENEATH COULD NOT CUT ME.";
 if(s->stage==3)return "I TUCKED THE BROKEN STITCH UNDER THE NEXT FOLD.";
 if(s->stage==4)return "I CLOSED MY HAND AGAIN. THE GRIP WAS SMOOTHER.";
 if(s->stage==5)return "FOR ONCE I HAD NOT READ THE NOTE IMMEDIATELY.";
 if(s->stage==6)return "A CLEAN RECTANGLE. FOUR SCREW HOLES. TWO BROKEN SCREWS.";
 if(s->stage==7)return s->tick<64?"I LOOKED AROUND THE BASE OF THE MACHINE.":s->tick<128?"UNDER THE BRAKE.":"IN THE POWDERY DUST BENEATH THE LADDER.";
 return "THERE WAS NO PLATE. PERHAPS IT HAD HELD THE FIGURES.";
}
static ht_joint ht_grip_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_grip_pose(const ht_game *game,const ht_grip_state *s){
 ht_game v=*game;unsigned t=s->tick,q=s->stage==1?ht_min(t,32)*256/32:s->stage>=2?256:0;
 int target=ht_grip_x()-8,wx=game->x/256+(target-game->x/256)*(int)q/256;
 v.x=wx*256;v.y=ht_surface_at(&v,4,wx)*256;v.vx=s->stage==1 && t<32?248:0;v.vy=0;v.facing=1;v.grounded=true;v.landing_ticks=0;v.stride=t*72;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(wx-v.camera/256,v.y/256-v.camera_y/256,&v);
 if(v.vx){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i)p.foot[i].x=(p.foot[i].x+wx-v.camera/256)/2;}
 if(s->stage && s->stage<5){
  unsigned reach=s->stage==1?(unsigned)ht_clamp((int)t-32,0,32)*256/32:256;
  ht_joint c=ht_grip_contact();c.x-=v.camera/256;c.y-=v.camera_y/256;p.hand[1]=ht_grip_mix(p.hand[1],c,reach);
 }
 return p;
}
/* The close view preserves a single handle, cloth wrap and hand path. The
 * first-person forearms enter from below; fingers contact the wool, not wood. */
static ht_joint ht_grip_stitch(const ht_grip_state *s){
 unsigned q=ht_grip_tucked(s)?256:s->stage==3?ht_min(s->tick,48)*256/48:0;
 return ht_grip_mix(ht_joint_at(260,169),ht_joint_at(242,134),q);
}
static ht_joint ht_grip_tucking_hand(const ht_grip_state *s){
 ht_joint rest=ht_joint_at(98,236);
 if(s->stage!=3)return rest;
 if(s->tick<24)return ht_grip_mix(rest,ht_grip_stitch(s),s->tick*256/24);
 if(s->tick<=64)return ht_grip_stitch(s);
 return ht_grip_mix(ht_grip_stitch(s),rest,ht_min(s->tick-64,32)*256/32);
}
static void ht_grip_insert(const ht_grip_state *s){
 ht_rect(ht_scene,0,0,480,226,59);
 for(int n=0;n<10;++n)ht_limb(ht_scene,0,28+n*21,479,21+n*21,0,0,76);
 ht_rect(ht_scene,281,18,171,208,150);ht_rect(ht_scene,293,24,150,195,119);
 ht_disk(ht_scene,317,160,44,195);ht_disk(ht_scene,317,160,34,83);ht_disk(ht_scene,317,160,25,148);
 ht_limb(ht_scene,317,160,144,108,7,7,192);
 /* The long split remains exposed only beyond the protective wool. */
 ht_limb(ht_scene,140,108,167,116,1,1,244);ht_limb(ht_scene,141,108,151,115,1,1,242);
 ht_limb(ht_scene,263,143,286,151,1,1,240);
 ht_limb(ht_scene,163,114,263,141,10,10,157);
 for(int n=0;n<18;++n){int x=164+n*5,y=114+n*27/20;ht_limb(ht_scene,x-2,y-8,x-7,y+8,1,0,n%3?105:202);}
 /* Thumb-worn patch and backward stitch use distinct woven geometry. */
 ht_limb(ht_scene,193,118,213,124,5,5,81);
 for(int n=0;n<4;++n)ht_limb(ht_scene,189+n*7,115+n*2,185+n*7,122+n*2,0,0,150);
 ht_limb(ht_scene,176,108,183,110,1,1,229);ht_limb(ht_scene,183,110,179,113,1,1,229);ht_limb(ht_scene,179,113,187,114,1,1,229);
 ht_joint stitch=ht_grip_stitch(s);ht_limb(ht_scene,246,133,stitch.x,stitch.y,1,0,215);
 unsigned loosen=s->stage==3?(unsigned)(s->tick<24?s->tick:s->tick<72?24:96-s->tick):0;
 int hand=213+(int)loosen/4;
 ht_limb(ht_scene,204,242,hand,154,13,9,231);ht_limb(ht_scene,hand,154,211,135,8,6,229);
 for(int n=0;n<3;++n)ht_limb(ht_scene,207+n*6,143+n,204+n*6,132+n,2,2,224);
 ht_limb(ht_scene,204,144,192,127,4,3,223);ht_limb(ht_scene,192,127,202,124,3,2,223);
 if(s->stage==3){ht_joint h=ht_grip_tucking_hand(s);ht_limb(ht_scene,81,248,h.x-15,h.y+13,9,6,231);ht_limb(ht_scene,h.x-15,h.y+13,h.x,h.y,5,2,229);ht_limb(ht_scene,h.x-10,h.y+3,h.x,h.y,2,1,227);}
 /* Folded notice still covers the pale former plate until deliberately lifted. */
 if(s->stage<5){ht_rect(ht_scene,316,36,111,49,45);for(int n=0;n<4;++n)ht_rect(ht_scene,326,45+n*8,83-n*9,1,148);}
}
static ht_joint ht_grip_hole(unsigned n){return ht_joint_at(n&1?373:155,n&2?148:54);}
static void ht_grip_plate(const ht_grip_state *s){
 ht_rect(ht_scene,0,0,480,226,128);
 for(int n=0;n<70;++n){int x=19+(int)(ht_hash(n+63)%441),y=18+(int)(ht_hash(n+84)%195);ht_rect(ht_scene,x,y,2,1,158);}
 ht_rect(ht_scene,144,43,240,116,66);ht_limb(ht_scene,144,159,384,159,1,1,178);
 for(unsigned n=0;n<4;++n){ht_joint h=ht_grip_hole(n);ht_disk(ht_scene,h.x,h.y,4,233);if(n==1 || n==2){ht_disk(ht_scene,h.x,h.y,2,120);ht_limb(ht_scene,h.x-2,h.y+1,h.x+1,h.y-2,1,0,210);}}
 unsigned lift=s->stage==5?ht_min(s->tick,80):80;
 int py=43-(int)lift*132/80;ht_rect(ht_scene,139,py,250,119,40);
 for(int n=0;n<6;++n)ht_rect(ht_scene,159,py+15+n*14,185-(n%3)*29,1,134);
 ht_limb(ht_scene,82,238,142,py+99,9,5,229);ht_disk(ht_scene,142,py+99,5,222);
 /* The other hand traces the rectangle edge without filling the missing plate. */
 ht_limb(ht_scene,411,240,387,154,10,6,232);ht_limb(ht_scene,387,154,374,158,4,2,224);
}
static void ht_grip_search(const ht_grip_state *s){
 unsigned focus=s->stage==8?2:ht_min(s->tick/64,2);
 ht_rect(ht_scene,0,0,480,226,focus==1?156:72);
 ht_rect(ht_scene,0,185,480,41,98);
 if(focus==0){ht_rect(ht_scene,83,0,74,190,174);ht_rect(ht_scene,317,0,63,188,171);ht_rect(ht_scene,69,184,101,9,201);ht_rect(ht_scene,303,182,93,11,201);}
 else if(focus==1){ht_disk(ht_scene,239,42,103,211);ht_disk(ht_scene,239,42,88,107);ht_rect(ht_scene,95,138,51,52,208);ht_rect(ht_scene,337,137,47,55,210);}
 else{ht_limb(ht_scene,88,222,153,0,7,7,191);ht_limb(ht_scene,293,222,354,0,7,7,191);for(int n=0;n<5;++n)ht_limb(ht_scene,101+n*12,176-n*39,305+n*12,176-n*39,5,5,194);}
 for(int n=0;n<91;++n){int x=14+(int)(ht_hash(n+202)%450),y=193+(int)(ht_hash(n+510)%29);ht_rect(ht_scene,x,y,1+n%4,1,n%3?121:150);}
 ht_limb(ht_scene,415,238,374,208,9,6,229);ht_limb(ht_scene,374,208,351,201,5,2,222);
}
static void ht_grip_study_render(const ht_game *game,const ht_grip_state *s){
 ht_game v=*game;v.camera=(ht_grip_x()-239)*256;v.camera_y=(ht_grip_floor()-185)*256;v.intimacy=256;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 if(s->stage<2){ht_person_pose p=ht_grip_pose(&v,s);ht_person_draw(&p);}
 ht_world_scale=256;
 if(s->stage>=2 && s->stage<=4)ht_grip_insert(s);
 else if(s->stage==5 || s->stage==6)ht_grip_plate(s);
 else if(s->stage>=7)ht_grip_search(s);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_grip_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A HOLD GRIP  START JOURNAL  X RETURN":s->stage==2?"A TUCK STITCH  START JOURNAL  X RETURN":s->stage==4?"A LIFT NOTICE  START JOURNAL  X RETURN":s->stage==6?"A LOOK AROUND  START NOTE  X RETURN":s->stage==8?"A READ NOTICE  START NOTE  X RETURN":"PAUSE HOLD  START JOURNAL  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
