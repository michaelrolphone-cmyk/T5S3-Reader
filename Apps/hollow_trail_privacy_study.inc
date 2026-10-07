/* Chapter VII: an optional pause behind the existing sacking, not a new room,
 * evidence site or route condition. All movement belongs to a frozen view. */
#define HT_PRIVACY_STUDY 1
#define HT_PRIVACY_X (HT_SLEEP_X+155)
#define HT_PRIVACY_INSIDE_X (HT_SLEEP_X+186)
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_privacy_state;
static ht_privacy_state ht_privacy;
static bool ht_privacy_near(const ht_game *g){return g->level==6 && g->grounded && !g->traversal.mode && ht_abs(g->x/256-HT_PRIVACY_X)<=7 && ht_abs(g->y/256-ht_surface_at(g,1,g->x/256))<=2;}
static void ht_privacy_begin(void){ht_privacy=(ht_privacy_state){true,false,0,0};}
static unsigned ht_privacy_limit(unsigned stage){return stage==1?128:stage==3?96:stage==5?224:stage==7?448:stage==9?160:0;}
static bool ht_privacy_step(ht_privacy_state *s){
 if(!s->active || s->paused)return false;
 unsigned limit=ht_privacy_limit(s->stage);if(!limit)return false;
 if(++s->tick==limit){if(s->stage==9)s->active=false;else{++s->stage;s->tick=0;}}return true;
}
static bool ht_privacy_action(ht_privacy_state *s){if(!s->active || s->paused || ht_privacy_limit(s->stage) || s->stage>8)return false;++s->stage;s->tick=0;return true;}
static const char *ht_privacy_label(const ht_privacy_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->stage<2)return "SOMEONE HAD CUT A CURTAIN FROM OLD SACKING.";
 if(s->stage==2)return "BEHIND IT A PERSON WOULD BE VISIBLE ONLY BY THEIR FEET.";
 if(s->stage==3 || s->stage==4)return "A BASIN, A TOWEL DRIED STIFF. A COMB WITH THREE TEETH MISSING.";
 if(s->stage==5)return s->tick<112?"I PRESSED A CLEAN FOLD OF THE TOWEL TO MY CHEEK.":"AND LEFT A PRINT OF QUARRY DUST.";
 if(s->stage==6)return "I LINGERED IN THAT IMPROVISED PRIVACY.";
 if(s->stage==7)return s->tick<224?"ONE CORNER SAGGED BECAUSE THE HOOK HAD COME AWAY.":"MY SISTER USED TO CATCH IT BACK WITH A PIN WHENEVER SHE PASSED.";
 if(s->stage==8)return "HERE SHE HAD FOUND A WALL WHERE THERE WAS NO WALL.";
 return "A ROOM INSIDE A RUINED BUILDING.";
}
static ht_joint ht_privacy_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_privacy_pose(const ht_game *game,const ht_privacy_state *s){
 ht_game v=*game;unsigned t=s->tick;int at=game->x/256;
 if(s->stage==1)at+=(HT_PRIVACY_INSIDE_X-at)*(int)ht_min(t,96)/96;
 else if(s->stage==9)at=HT_PRIVACY_INSIDE_X+(at-HT_PRIVACY_INSIDE_X)*(int)ht_min(t,112)/112;
 else if(s->stage>=2)at=HT_PRIVACY_INSIDE_X;
 bool walking=(s->stage==1 && t<96) || (s->stage==9 && t<112);
 v.x=at*256;v.y=ht_surface_at(&v,1,at)*256;v.vx=walking?(s->stage==9?-224:224):0;v.vy=0;v.facing=s->stage==9?-1:1;v.grounded=true;v.landing_ticks=0;v.stride=t*70;memset(&v.traversal,0,sizeof(v.traversal));
 int x=at-v.camera/256,f=(v.y-v.camera_y)/256;ht_person_pose p=ht_person_pose_at(x,f,&v);
 if(walking){++p.hip.y;++p.shoulder.y;for(int i=0;i<2;++i){int lift=ht_person_ground(&v,p.foot[i].x,f)-p.foot[i].y;p.foot[i].x=x+(p.foot[i].x-x)/2;p.foot[i].y=ht_person_ground(&v,p.foot[i].x,f)-lift;}}
 if(s->stage==9 && t>=112){ht_person_pose end=ht_person_pose_at((game->x-game->camera)/256,(game->y-game->camera_y)/256,game);unsigned q=ht_min(t-112,48)*256/48;p.hip=ht_privacy_mix(p.hip,end.hip,q);p.shoulder=ht_privacy_mix(p.shoulder,end.shoulder,q);for(int i=0;i<2;++i){p.hand[i]=ht_privacy_mix(p.hand[i],end.hand[i],q);p.foot[i]=ht_privacy_mix(p.foot[i],end.foot[i],q);}}
 return p;
}
/* One path supplies both the clean corner and the grasp. Dust appears only
 * after actual cheek contact, and remains on that fold as it is lowered. */
static ht_joint ht_privacy_fold(const ht_privacy_state *s){
 unsigned q=0;if(s->stage==5){unsigned t=s->tick;q=t<48?0:t<96?(t-48)*256/48:t<144?256:t<192?(192-t)*256/48:0;}
 return ht_privacy_mix(ht_joint_at(247,130),ht_joint_at(250,124),q);
}
static bool ht_privacy_dust(const ht_privacy_state *s){return s->stage>=6 || (s->stage==5 && s->tick>=112);}
static ht_person_pose ht_privacy_towel_pose(const ht_game *game,const ht_privacy_state *s){
 ht_game v=*game;v.grounded=true;v.vx=v.vy=0;v.facing=-1;v.landing_ticks=0;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(256,151,&v);p.hip=ht_joint_at(256,136);p.shoulder=ht_joint_at(253,128);p.foot[0]=ht_joint_at(254,151);p.foot[1]=ht_joint_at(259,151);p.hand[0]=ht_joint_at(253,139);p.hand[1]=ht_joint_at(251,138);p.facing=-1;
 if(s->stage==5){unsigned t=s->tick,q=t<48?t*256/48:t<192?256u:(224u-(unsigned)ht_min(t,224))*256/32;ht_joint fold=ht_privacy_fold(s);p.hand[1]=ht_privacy_mix(p.hand[1],ht_joint_at(fold.x-2,fold.y+6),q);}
 return p;
}
static bool ht_privacy_comb_tooth(unsigned n){return n<16 && n!=3 && n!=8 && n!=12;}
static void ht_privacy_interior(const ht_game *g,const ht_privacy_state *s){
 ht_rect(ht_scene,0,0,480,226,73);ht_rect(ht_scene,0,211,480,15,178);
 /* Reverse of the same sewn curtain, with a narrow strip of glasshouse light. */
 ht_triangle(ht_scene,20,0,65,0,75,212,137);ht_triangle(ht_scene,20,0,75,212,18,215,145);
 for(int n=0;n<14;++n)ht_limb(ht_scene,40,7+n*14,47,13+n*14,1,0,208);
 ht_limb(ht_scene,88,0,88,214,3,3,201);ht_disk(ht_scene,66,210,7,223);
 /* Shelf comb: thirteen teeth and exactly three exposed gaps. */
 ht_limb(ht_scene,143,68,257,68,3,3,183);ht_limb(ht_scene,149,69,149,81,2,1,190);
 ht_rect(ht_scene,166,45,81,5,225);for(unsigned n=0;n<16;++n)if(ht_privacy_comb_tooth(n))ht_limb(ht_scene,168+(int)n*5,50,168+(int)n*5,61,1,1,226);
 ht_limb(ht_scene,163,43,247,43,1,1,125);
 /* A shallow enamel basin on its own stand, beside the stiff hanging towel. */
 ht_limb(ht_scene,142,183,138,214,2,2,201);ht_limb(ht_scene,223,183,229,214,2,2,201);
 ht_triangle(ht_scene,127,172,238,172,224,192,175);ht_triangle(ht_scene,127,172,224,192,141,192,175);
 ht_limb(ht_scene,125,171,239,171,3,3,215);ht_limb(ht_scene,135,168,230,168,2,2,43);
 ht_limb(ht_scene,243,103,285,103,2,2,178);
 ht_person_pose p=ht_privacy_towel_pose(g,s);ht_world_scale=1280;ht_person_draw(&p);
 ht_joint c=ht_privacy_fold(s);bool dusty=ht_privacy_dust(s);
 ht_triangle(ht_scene,c.x-7,c.y,c.x,c.y,c.x-1,c.y+14,12);ht_triangle(ht_scene,c.x-7,c.y,c.x-1,c.y+14,c.x-8,c.y+13,24);
 ht_limb(ht_scene,c.x-7,c.y,c.x,c.y,0,0,126);ht_limb(ht_scene,c.x,c.y,c.x-1,c.y+14,0,0,126);ht_limb(ht_scene,c.x-7,c.y,c.x-8,c.y+13,0,0,110);
 ht_limb(ht_scene,c.x-7,c.y+1,c.x-1,c.y+1,0,0,117);ht_limb(ht_scene,c.x-7,c.y,c.x,c.y,0,0,126);ht_limb(ht_scene,c.x,c.y,c.x-1,c.y+14,0,0,126);ht_limb(ht_scene,c.x-7,c.y,c.x-8,c.y+13,0,0,110);
 ht_limb(ht_scene,c.x-7,c.y+12,c.x-2,c.y+13,0,0,145);
 for(int n=0;n<4;++n)ht_limb(ht_scene,c.x-7+n*2,c.y+3,c.x-8+n*2,c.y+10,0,0,87);
 if(dusty){ht_limb(ht_scene,c.x-2,c.y+1,c.x-2,c.y+4,1,1,155);ht_disk(ht_scene,c.x-3,c.y+4,1,169);ht_rect(ht_scene,c.x-4,c.y+2,1,1,143);}
 /* The near forearm stays outside the cloth; drawing only its fingers
  * here would leave a detached hand when the towel covers the wrist. */
 if(s->stage==5 && s->tick>0 && s->tick<224){ht_person_arm(p.shoulder,p.hand[1],p.elbow[1],255);ht_disk(ht_scene,p.hand[1].x,p.hand[1].y,1,224);}
 ht_world_scale=256;
}
/* Two remembered passes, each bounded to 224 ticks. The second is a cut to
 * another small repair, not a claim that her pin immediately falls out. */
static unsigned ht_privacy_pin_lift(unsigned t){return t<96?0:t<128?(t-96)*256/32:256;}
static ht_person_pose ht_privacy_memory_pose(const ht_game *game,unsigned tick){
 unsigned t=tick%224;ht_game v=*game;int x=t<64?279-(int)t*26/64:t<176?253:253-(int)(t-176)*27/48;
 v.vx=t<64 || t>=176?-224:0;v.vy=0;v.facing=-1;v.grounded=true;v.landing_ticks=0;v.stride=t*70;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(x,151,&v);p.shoulder=ht_joint_at(x,128);p.hip=ht_joint_at(x,136);p.foot[0]=ht_joint_at(x-2,151);p.foot[1]=ht_joint_at(x+2,151);
 if(v.vx)for(int i=0;i<2;++i){int phase=(int)((v.stride/256+i*16)%32),step=phase<16?8-phase:phase-24,lift=phase<16?0:(phase-16)*(32-phase)*3/64;p.foot[i]=ht_joint_at(x-step/2,151-lift);}
 p.hand[0]=ht_joint_at(x-2,138);p.hand[1]=ht_joint_at(x-4,137);
 unsigned q=t<64?0:t<96?(t-64)*256/32:t<152?256:t<176?(176-t)*256/24:0;
 ht_joint corner=ht_joint_at(249,129-(int)ht_privacy_pin_lift(t)*8/256);p.hand[1]=ht_privacy_mix(p.hand[1],corner,q);p.elbow[1]=-1;
 return p;
}
static void ht_privacy_memory(const ht_game *g,unsigned tick){
 unsigned t=tick%224,lift=ht_privacy_pin_lift(t);int corner=105-(int)lift*40/256;
 ht_rect(ht_scene,0,0,480,226,68);ht_rect(ht_scene,25,17,430,184,77);ht_rect(ht_scene,0,201,480,25,139);
 for(int n=0;n<7;++n)ht_limb(ht_scene,10+n*74,201,2+n*78,226,0,0,165);
 ht_rect(ht_scene,112,34,181,157,21);ht_limb(ht_scene,111,33,296,33,3,3,193);ht_limb(ht_scene,112,33,112,193,3,3,188);ht_limb(ht_scene,292,33,292,193,3,3,185);ht_limb(ht_scene,111,193,294,193,3,3,204);
 ht_limb(ht_scene,199,35,199,191,2,2,121);ht_limb(ht_scene,113,120,291,120,2,2,111);
 ht_triangle(ht_scene,113,47,285,corner,286,190,143);ht_triangle(ht_scene,113,47,286,190,118,191,136);
 for(int n=0;n<8;++n){int x=125+n*19;ht_limb(ht_scene,x,51+(corner-47)*(x-113)/172,x+2,185,1,1,158);}
 /* The missing hook leaves a broken plaster mark; the pin catches below it. */
 ht_triangle(ht_scene,281,52,293,54,285,65,128);ht_limb(ht_scene,284,53,290,56,1,1,209);
 ht_person_pose p=ht_privacy_memory_pose(g,tick);ht_world_scale=1280;ht_person_draw(&p);ht_world_scale=256;
 if(t>=128){ht_limb(ht_scene,284,62,289,67,1,1,221);ht_disk(ht_scene,289,67,1,222);}
}
static void ht_privacy_study_render(const ht_game *game,const ht_privacy_state *s){
 ht_game v=*game;v.camera=(HT_PRIVACY_INSIDE_X-240)*256;v.camera_y=(ht_sleep_floor()-170)*256;v.intimacy=512;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 ht_person_pose p=ht_privacy_pose(&v,s);ht_person_draw(&p);ht_sleep_screen(&v);ht_world_scale=256;
 if(s->stage>=3 && s->stage<=6)ht_privacy_interior(game,s);
 if(s->stage==7)ht_privacy_memory(game,s->tick);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_privacy_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A STEP BEHIND  START NOTES  X RETURN":s->stage==2?"A LOOK INSIDE  START NOTES  X RETURN":s->stage==4?"A TAKE CLEAN FOLD  START NOTES  X RETURN":s->stage==6?"A REMEMBER  START NOTES  X RETURN":s->stage==8?"A STEP OUT  START NOTES  X RETURN":"PAUSE HOLD  START NOTES  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_privacy_prompt(const ht_game *g){if(!ht_privacy_near(g))return;ht_rect(ht_scene,138,221,204,15,255);ht_text_color(148,225,"A STEP BEHIND THE SACKING",1,85);}
