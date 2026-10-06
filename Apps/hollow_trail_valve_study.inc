/* Optional Chapter VIII examination. Only ht_inspect records item one;
 * this state never writes the live body, storm, distributor or reading gates. */
#define HT_VALVE_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_valve_state;
static ht_valve_state ht_valve;
static bool ht_valve_near(const ht_game *g){return g->level==7 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==1;}
static void ht_valve_begin(void){ht_valve=(ht_valve_state){true,false,0,0};}
static unsigned ht_valve_limit(unsigned stage){return stage==1?64:stage==3 || stage==5 || stage==7 || stage==9?96:0;}
static bool ht_valve_step(ht_valve_state *s){
 if(!s->active || s->paused || !ht_valve_limit(s->stage))return false;
 if(++s->tick==ht_valve_limit(s->stage)){++s->stage;s->tick=0;}return true;
}
static bool ht_valve_action(ht_valve_state *s){if(!s->active || s->paused || (s->stage&1) || s->stage>=10)return false;++s->stage;s->tick=0;return true;}
static const char *ht_valve_label(const ht_valve_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "THE COLD GAVE ME WORK TO DO.";
 if(s->stage==1)return "I SPREAD THE NOTEBOOK ON A DRY LEDGE.";
 if(s->stage==2)return "THE OILCLOTH HAD PROTECTED THE CENTRE.";
 if(s->stage==3)return "I SCRAPED THE LIME CLEAN WITH A FINGERNAIL.";
 if(s->stage==4)return "THE PIPE RAN BACK TOWARD THE GARDENS.";
 if(s->stage==5)return "HER NOTE WAS TIED TO THE VALVE WHEEL.";
 if(s->stage==6)return "HIS ACCOUNT LAY IN THE MAINTENANCE BOX.";
 if(s->stage==7)return "FOLDED BEHIND IT: A REQUEST FOR EXTRA RIDGE POWER.";
 if(s->stage==8)return "IT BORE HIS SIGNATURE.";
 if(s->stage==9)return "RAIN STIPPLED THE PAPER. I PULLED IT UNDER SHELTER.";
 return "HIS SIGNATURE PLACED HIS HAND ON A VALVE OF HIS OWN.";
}
static ht_joint ht_valve_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
/* Actual wheel and body share the same reachable approach/contact in overview. */
static ht_person_pose ht_valve_pose(const ht_game *game,const ht_valve_state *s){
 ht_game v=*game;unsigned q=s->stage==5?ht_min(s->tick,32)*8:0;
 int target=ht_valve_x()+4,wx=game->x/256+(target-game->x/256)*(int)q/256;
 v.x=wx*256;v.y=ht_surface_at(&v,4,wx)*256;v.vx=v.vy=0;v.facing=1;v.grounded=true;v.landing_ticks=0;v.stride=0;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(wx-v.camera/256,v.y/256-v.camera_y/256,&v);
 if(s->stage==5){unsigned reach=(unsigned)ht_clamp((int)s->tick-32,0,32)*8;ht_joint c=ht_valve_note_contact();c.x-=v.camera/256;c.y-=v.camera_y/256;p.hand[1]=ht_valve_mix(p.hand[1],c,reach);}
 return p;
}
static void ht_valve_forearm(ht_joint h,bool right){
 int wrist=right?397:78;ht_limb(ht_scene,wrist,239,h.x+(right?13:-13),h.y+19,10,6,231);
 ht_limb(ht_scene,h.x+(right?13:-13),h.y+19,h.x,h.y,5,2,222);
 ht_disk(ht_scene,h.x,h.y,3,220);
 /* Pale pressure marks and scraped skin stay on the same fingers. */
 ht_limb(ht_scene,h.x-2,h.y+4,h.x+2,h.y+2,1,0,143);
 ht_limb(ht_scene,h.x+2,h.y+8,h.x+4,h.y+6,0,0,133);
}
static unsigned ht_valve_clean(const ht_valve_state *s){return s->stage>3?96:s->stage==3?ht_min(s->tick,96):0;}
static ht_joint ht_valve_nail(const ht_valve_state *s){unsigned q=ht_valve_clean(s);return ht_joint_at(148+(int)q*184/96,98+(int)(q%12)/3);}
static void ht_valve_notebook(const ht_valve_state *s){
 ht_rect(ht_scene,0,0,480,226,124);ht_rect(ht_scene,27,184,424,21,183);
 /* Wrinkled oilcloth surrounds a dark wet margin and a dry open centre. */
 ht_triangle(ht_scene,77,38,388,55,422,180,154);ht_triangle(ht_scene,77,38,422,180,57,178,154);
 for(int n=0;n<12;++n)ht_limb(ht_scene,63+n*29,180,82+n*25,52,0,0,134);
 int q=s->stage==1?ht_min(s->tick,64):64,left=235-q*116/64;
 ht_rect(ht_scene,left-8,59,255-left,116,120);ht_rect(ht_scene,247,64,117,114,108);
 ht_rect(ht_scene,left,66,238-left,103,43);ht_rect(ht_scene,249,69,106,102,53);
 ht_limb(ht_scene,244,66,244,174,1,1,187);
 for(int n=0;n<7;++n){ht_rect(ht_scene,left+8,77+n*12,ht_max(0,222-left),1,135);ht_rect(ht_scene,258,80+n*12,86,1,139);}
 /* The knife weights the pages; no automatic drying or new writing. */
 ht_limb(ht_scene,181,153,278,162,3,2,222);ht_limb(ht_scene,278,162,321,165,4,4,178);
 ht_valve_forearm(ht_joint_at(left+4,168),false);ht_valve_forearm(ht_joint_at(351,172),true);
}
static void ht_valve_pipe(const ht_valve_state *s){
 ht_rect(ht_scene,0,0,480,226,121);ht_rect(ht_scene,0,52,480,113,150);
 ht_limb(ht_scene,0,57,479,57,2,2,103);ht_limb(ht_scene,0,163,479,163,3,3,212);
 for(int x=43;x<480;x+=354){ht_rect(ht_scene,x,47,18,122,199);for(int n=0;n<3;++n)ht_disk(ht_scene,x+9,62+n*41,4,227);}
 ht_rect(ht_scene,132,80,219,44,69);ht_text_color(155,94,"TO GARDENS",2,204);
 unsigned clean=ht_valve_clean(s);int edge=146+(int)clean*188/96;
 for(int n=0;n<140;++n){int x=140+(int)(ht_hash(n+291)%203),y=83+(int)(ht_hash(n+518)%38);if(x>edge || !clean)ht_disk(ht_scene,x,y,3+(int)(n%3),105+n%4*8);}
 if(s->stage==3){ht_joint h=ht_valve_nail(s);ht_valve_forearm(h,true);ht_limb(ht_scene,h.x-3,h.y-1,h.x+1,h.y-1,1,0,70);for(int n=0;n<9;++n)ht_rect(ht_scene,h.x-17+n*3,130+(n*11+(int)clean)%49,2,2,91);}
}
/* Every sheet and its supporting hand use these anchors during extraction,
 * comparison and sheltering; the two statements always share the valve cover. */
static ht_joint ht_valve_paper(const ht_valve_state *s,bool keeper){
 ht_joint rest=ht_joint_at(keeper?275:75,57),from=ht_joint_at(keeper?362:93,keeper?173:19);
 if(s->stage==5)rest=ht_valve_mix(from,rest,(unsigned)ht_clamp((int)s->tick-72,0,24)*256/24);
 unsigned shelter=s->stage==9?ht_min(s->tick,80):s->stage>=10?80:0;
 rest.x-=(int)shelter*24/80;rest.y+=(int)shelter*18/80;return rest;
}
static ht_joint ht_valve_paper_hand(const ht_valve_state *s,bool keeper){ht_joint p=ht_valve_paper(s,keeper);return ht_joint_at(p.x+(keeper?135:5),p.y+130);}
static unsigned ht_valve_request(const ht_valve_state *s){return s->stage>7?96:s->stage==7?ht_min(s->tick,96):0;}
static void ht_valve_lines(int x,int y,const char *a,const char *b,const char *c){ht_text_color(x,y,a,1,198);ht_text_color(x,y+12,b,1,198);ht_text_color(x,y+24,c,1,198);}
static void ht_valve_papers(const ht_valve_state *s){
 ht_rect(ht_scene,0,0,480,226,116);
 ht_rect(ht_scene,26,180,428,26,195);ht_triangle(ht_scene,26,180,75,31,437,41,167);ht_triangle(ht_scene,26,180,437,41,454,180,167);
 for(int n=0;n<4;++n)ht_disk(ht_scene,47+n*124,190,4,231);
 /* Box remains at the edge; its lifted lid exposes the account and folded request. */
 ht_rect(ht_scene,394,112,73,76,212);ht_rect(ht_scene,401,120,58,61,135);ht_triangle(ht_scene,395,112,459,77,467,114,197);
 for(int side=0;side<2;++side){
  ht_joint p=ht_valve_paper(s,side!=0);int x=p.x,y=p.y;
  ht_rect(ht_scene,x-2,y+2,144,133,215);ht_rect(ht_scene,x,y,140,130,side?62:40);
  if(!side){
   ht_limb(ht_scene,x+4,y-5,x+9,y+8,0,0,211);ht_text_color(x+12,y+16,"HER LIMIT",1,220);
   ht_valve_lines(x+8,y+41,"DO NOT TRADE","THEIR HEAT","FOR MY CALL.");
   ht_valve_lines(x+8,y+82,"IF SOMEONE DEMANDS","MORE FOR THE RIDGE,","SHOW THEM THIS.");
  }else{
   ht_text_color(x+9,y+16,"KEEPER'S ACCOUNT",1,216);
   ht_valve_lines(x+8,y+40,"SHE CONTROLLED","THE COMMON","SUPPLY.");
   unsigned q=ht_valve_request(s);int ry=y+111-(int)q*100/96;
   ht_rect(ht_scene,x+3,ry,136,ht_min(128,y+130-ry),79);
   if(q>=40){
    ht_text_color(x+10,ry+12,"EXTRA POWER",1,213);ht_text_color(x+10,ry+24,"TO THE RIDGE",1,213);
    if(q>=88){
     ht_rect(ht_scene,x+12,ry+72,109,1,159);
     /* A handwritten signature, without inventing a legible keeper name. */
     for(int n=0;n<9;++n)ht_limb(ht_scene,x+15+n*10,ry+89+(n%2)*5,x+24+n*10,ry+85+(n%3)*3,1,0,228);
     ht_limb(ht_scene,x+19,ry+105,x+114,ry+96,0,0,226);
    }
   }
   ht_limb(ht_scene,x+3,ry,x+138,ry,1,1,163);
  }
  ht_valve_forearm(ht_valve_paper_hand(s,side!=0),side!=0);
 }
 if(s->stage>=9){
  unsigned q=s->stage==9?ht_min(s->tick,80):80;
  /* Only these exposed papers get stippled, then move beneath the overhang.
   * The existing world storm still starts with the original acquisition. */
  for(int n=0;n<24;++n){int x=78+(int)(ht_hash(n+407)%330),y=70+(int)(ht_hash(n+731)%101);if(x>80+(int)q*4)ht_disk(ht_scene,x,y,1,136);}
  ht_triangle(ht_scene,0,0,360,0,43+(int)q*4,30,201);
 }
}
static void ht_valve_study_render(const ht_game *game,const ht_valve_state *s){
 ht_game v=*game;v.camera=(ht_valve_x()-239)*256;v.camera_y=(ht_valve_floor()-187)*256;v.intimacy=256;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 if(!s->stage || (s->stage==5 && s->tick<72)){ht_person_pose p=ht_valve_pose(&v,s);ht_person_draw(&p);}
 ht_world_scale=256;
 if(s->stage==1 || s->stage==2)ht_valve_notebook(s);
 else if(s->stage==3 || s->stage==4)ht_valve_pipe(s);
 else if(s->stage>=5 && (s->stage!=5 || s->tick>=72))ht_valve_papers(s);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_valve_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A SPREAD NOTEBOOK  START NOTE  X RETURN":s->stage==2?"A CLEAN LABEL  START NOTE  X RETURN":s->stage==4?"A TAKE PAPERS  START NOTE  X RETURN":s->stage==6?"A UNFOLD REQUEST  START NOTE  X RETURN":s->stage==8?"A SHELTER PAPERS  START NOTE  X RETURN":s->stage==10?"A READ NOTE  START NOTE  X RETURN":"PAUSE HOLD  START NOTE  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
