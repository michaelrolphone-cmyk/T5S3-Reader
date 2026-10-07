/* One strip: tied -> carried -> worn. Only completed physical transfers
 * change ownership; cancellation and checkpoint retry retain that state. */
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_scarf_state;
static ht_scarf_state ht_scarf;
static bool ht_scarf_near(const ht_game *g){return g->level==8 && g->grounded && !g->traversal.mode && !g->door_stage && ht_evidence_near(g)==0;}
static void ht_scarf_begin(void){ht_scarf=(ht_scarf_state){true,false,(uint8_t)(ht.scarf>=2?10:ht.scarf?4:0),0};}
static unsigned ht_scarf_limit(unsigned stage){return stage==1?64:stage==3 || stage==5 || stage==7 || stage==9?96:0;}
static bool ht_scarf_step(ht_scarf_state *s){
 if(!s->active || s->paused || !ht_scarf_limit(s->stage))return false;
 if(++s->tick==ht_scarf_limit(s->stage)){
  ++s->stage;s->tick=0;
  if(s->stage==4)ht.scarf=1;
  if(s->stage==10)ht.scarf=2;
 }return true;
}
static bool ht_scarf_action(ht_scarf_state *s){if(!s->active || s->paused || (s->stage&1) || s->stage>=10)return false;++s->stage;s->tick=0;return true;}
static const char *ht_scarf_label(const ht_scarf_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "THE LARGER PIECE, TIED BELOW AN OLD GUY WIRE.";
 if(s->stage==1)return "TWO FINGERS BENEATH THE FOLD. BACKWARD STITCHES.";
 if(s->stage==2)return "FOR A MOMENT I FELT ONLY THE CLOTH.";
 if(s->stage==3)return "THEN I UNTIED IT.";
 if(s->stage==4)return "THE POST HELD A TRACING BENEATH A METAL CAP.";
 if(s->stage==5)return "I SPREAD THE SCARF OVER THE TRACING.";
 if(s->stage==6)return "TWO TURNS SIDE BY SIDE: ONE IN THREAD, ONE IN INK.";
 if(s->stage==7)return "THE DATE WAS EARLIER THAN THE FIRST HOLDING LOCK.";
 if(s->stage==8)return "I THOUGHT I WAS FOLLOWING AN ORDER FROM ELSEWHERE.";
 if(s->stage==9)return "I WRAPPED IT AROUND MY NECK, INSIDE MY COLLAR.";
 return "IT WAS NOT ENOUGH TO MAKE ME WARM.";
}
static ht_joint ht_scarf_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_scarf_pose(const ht_game *game,const ht_scarf_state *s){
 ht_game v=*game;unsigned q=s->stage==1?ht_min(s->tick,32)*8:s->stage?256:0;
 int wx=game->x/256+(ht_scarf_x()-4-game->x/256)*(int)q/256;
 v.x=wx*256;v.y=ht_surface_at(&v,1,wx)*256;v.vx=v.vy=0;v.facing=1;v.grounded=true;v.landing_ticks=0;v.stride=0;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(wx-v.camera/256,v.y/256-v.camera_y/256,&v);
 if(s->stage==1 || s->stage==2){ht_joint k=ht_scarf_knot();k.x-=v.camera/256;k.y-=v.camera_y/256;
  p.hand[1]=ht_scarf_mix(p.hand[1],k,s->stage==1?(unsigned)ht_clamp((int)s->tick-32,0,32)*8:256);}
 return p;
}
/* The close view magnifies that same post, cap, cloth and tracing. Endpoints
 * also drive both hands, so thread follows the strip through every move. */
static ht_joint ht_scarf_detail(ht_joint p){return ht_joint_at(210+(p.x-ht_scarf_x())*5,225+(p.y-ht_scarf_floor())*5);}
static ht_joint ht_scarf_end(const ht_scarf_state *s,bool right){
 ht_joint k=ht_scarf_detail(ht_scarf_knot());
 ht_joint tied=ht_joint_at(k.x+(right?3:-3),k.y+(right?70:0));
 ht_joint loose=ht_joint_at(right?265:80,157),spread=ht_joint_at(right?355:103,139),neck=ht_joint_at(right?353:272,49);
 if(s->stage<3)return tied;
 if(s->stage==3)return ht_scarf_mix(tied,loose,(unsigned)ht_clamp((int)s->tick-32,0,64)*4);
 if(s->stage==4)return loose;
 if(s->stage==5)return ht_scarf_mix(loose,spread,ht_min(s->tick,96)*256/96);
 if(s->stage<9)return spread;
 return ht_scarf_mix(spread,neck,s->stage==9?ht_min(s->tick,96)*256/96:256);
}
static ht_joint ht_scarf_thread(const ht_scarf_state *s){return ht_scarf_mix(ht_scarf_end(s,false),ht_scarf_end(s,true),176);}
static ht_joint ht_scarf_ink(void){return ht_joint_at(278,113);}
static void ht_scarf_turn(ht_joint p,int tone){
 ht_limb(ht_scene,p.x-10,p.y-3,p.x+7,p.y+3,1,1,tone);
 ht_limb(ht_scene,p.x+7,p.y+3,p.x+1,p.y-3,1,1,tone);
 ht_limb(ht_scene,p.x+1,p.y-3,p.x+12,p.y-3,1,0,tone);
}
static void ht_scarf_hand(ht_joint p,bool right){
 ht_limb(ht_scene,right?455:29,236,p.x+(right?25:-25),p.y+25,12,7,226);
 ht_limb(ht_scene,p.x+(right?25:-25),p.y+25,p.x,p.y,6,2,215);
 /* Two fingers beneath the same fold, not a detached pointing marker. */
 ht_limb(ht_scene,p.x,p.y,p.x+4,p.y-3,2,1,211);ht_limb(ht_scene,p.x-3,p.y+1,p.x+1,p.y-2,1,1,211);
}
static void ht_scarf_insert(const ht_scarf_state *s){
 ht_rect(ht_scene,0,0,480,226,103);
 ht_rect(ht_scene,145,0,31,226,213);for(int n=0;n<4;++n)ht_limb(ht_scene,150+n*5,0,152+n*5,226,0,0,174+n*6);
 ht_limb(ht_scene,151,57,463,208,1,1,161); /* old guy wire */
 ht_rect(ht_scene,159,45,159,111,58);ht_rect(ht_scene,143,35,180,9,234);
 /* Precise floors, cables, two cabinet faces. The small backward mark is
  * beside the master compartment, never an invented verdict or signature. */
 ht_rect(ht_scene,223,57,48,54,156);ht_rect(ht_scene,225,59,44,50,58);
 for(int n=0;n<4;++n)ht_limb(ht_scene,225,66+n*12,269,66+n*12,0,0,165);
 ht_limb(ht_scene,239,59,239,112,0,0,177);
 for(int n=0;n<3;++n){int x=170+n*16;ht_triangle(ht_scene,x,96,x+8,90,x+16,96,163);ht_rect(ht_scene,x+2,96,12,14,150);}
 ht_rect(ht_scene,279,70,30,25,166);ht_rect(ht_scene,281,72,11,21,70);ht_rect(ht_scene,296,72,11,21,70);
 ht_limb(ht_scene,269,84,279,84,0,0,168);ht_scarf_turn(ht_scarf_ink(),185);
 if(s->stage>=7){
  /* Hand silhouette through wind-shaken thin paper, no extra document. */
  int shake=s->stage==7?(int)(s->tick/12)%3-1:0;
  ht_limb(ht_scene,159,171,195,119+shake,8,5,120);ht_limb(ht_scene,195,119+shake,216,106+shake,4,2,114);
  ht_text_color(22,178,"DATE: BEFORE THE FIRST HOLDING LOCK",1,223);
  ht_text_color(22,192,"I THOUGHT I WAS FOLLOWING",1,223);ht_text_color(22,204,"AN ORDER FROM ELSEWHERE.",1,223);
 }
 if(s->stage==9){
  ht_rect(ht_scene,0,0,480,226,103);
  /* Close on the traveler's existing neck and open coat. The same strip
   * rises from both hands and settles inside the collar, never offscreen. */
  ht_rect(ht_scene,294,0,37,53,219);
  ht_triangle(ht_scene,243,45,309,62,257,226,236);
  ht_triangle(ht_scene,363,45,316,62,409,226,231);
  ht_triangle(ht_scene,257,226,309,62,409,226,225);
  ht_limb(ht_scene,243,45,290,69,3,2,173);ht_limb(ht_scene,363,45,337,70,3,2,168);
 }
 ht_joint a=ht_scarf_end(s,false),b=ht_scarf_end(s,true);
 ht_limb(ht_scene,a.x,a.y,b.x,b.y,10,10,143);
 for(int n=0;n<=28;++n){ht_joint p=ht_scarf_mix(a,b,(unsigned)n*256/28);
  ht_limb(ht_scene,p.x-3,p.y-8,p.x+3,p.y+8,0,0,n%3?118:178);}
 /* Protected weave stays tight; irregular ends remain frayed after taking. */
 for(int n=0;n<6;++n){ht_limb(ht_scene,b.x-8+n*3,b.y+8,b.x-11+n*3,b.y+14-n%3,0,0,137);}
 ht_scarf_turn(ht_scarf_thread(s),217);
 if(s->stage==2 || (s->stage==3 && s->tick<32)){
  ht_limb(ht_scene,155,100,a.x,a.y,4,3,146);ht_limb(ht_scene,a.x-6,a.y,a.x+6,a.y,3,2,157);
 }
 ht_scarf_hand(a,false);ht_scarf_hand(b,true);ht_checkpoint();
}
static void ht_scarf_study_render(const ht_game *game,const ht_scarf_state *s){
 ht_game v=*game;v.camera=(ht_scarf_x()-225)*256;v.camera_y=(ht_scarf_floor()-192)*256;
 v.intimacy=256;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);
 ht_world_scale=ht_scene_scale(&v);
 if(s->stage<2 || s->stage==10){ht_person_pose p=ht_scarf_pose(&v,s);ht_person_draw(&p);ht_person_scarf(&p,game->scarf);}
 ht_world_scale=256;if(s->stage>=2 && s->stage<10)ht_scarf_insert(s);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_scarf_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A FEEL CLOTH  START NOTE  X RETURN":s->stage==2?"A UNTIE  START NOTE  X RETURN":s->stage==4?"A COMPARE TURNS  START NOTE  X RETURN":s->stage==6?"A READ TRACING  START NOTE  X RETURN":s->stage==8?"A WRAP SCARF  START NOTE  X RETURN":s->stage==10?"A READ NOTE  START NOTE  X RETURN":"PAUSE HOLD  START NOTE  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
