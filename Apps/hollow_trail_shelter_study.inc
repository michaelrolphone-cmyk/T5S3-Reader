/* Optional rest below the pass. Odd stages are bounded motions; even stages
 * wait for a fresh confirmation. Stage five is a short night-to-dawn cutscene.
 * Every camera/body/ice change belongs to a copy, never the live route. */
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_shelter_state;
static ht_shelter_state ht_shelter;
static bool ht_shelter_near(const ht_game *g){return g->level==8 && g->grounded && !g->traversal.mode && !g->door_stage && ht_abs(g->x/256-HT_SHELTER_X)<=20 && ht_abs(g->y/256-ht_surface_at(g,0,g->x/256))<=3;}
static void ht_shelter_begin(void){ht_shelter=(ht_shelter_state){true,false,0,0};}
static unsigned ht_shelter_limit(const ht_shelter_state *s){
 if(!(s->stage&1) || s->stage>13)return 0;
 return s->stage==5?320:s->stage==1 || s->stage==13?160:s->stage==9?96:128;
}
static bool ht_shelter_step(ht_shelter_state *s){
 if(!s->active || s->paused || !ht_shelter_limit(s))return false;
 if(++s->tick==ht_shelter_limit(s)){++s->stage;s->tick=0;}return true;
}
static bool ht_shelter_action(ht_shelter_state *s){if(!s->active || s->paused || (s->stage&1) || s->stage>=14)return false;++s->stage;s->tick=0;return true;}
static const char *ht_shelter_label(const ht_shelter_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "A BUNK FIXED TO THE WALL BELOW THE PASS.";
 if(s->stage==1)return "I LAY DOWN IN MY DAMP CLOTHES.";
 if(s->stage==2)return "I WOKE IN THE COLDEST PART OF THE NIGHT.";
 if(s->stage==3)return "I PUT THE DRY STOCKINGS ON MY HANDS.";
 if(s->stage==4)return "THE STOCKINGS HUNG BEYOND MY FINGERS.";
 if(s->stage==5){if(s->tick<96)return "I TURNED WHENEVER MY RIBS TOUCHED THE BUNK.";if(s->tick<192)return "BY MORNING, THE CLOTH HELD DARK KNUCKLE MARKS.";return "AT DAWN, ICE HAD FORMED IN THE WASHBASIN.";}
 if(s->stage==6 || s->stage==7)return "AT DAWN, ICE HAD FORMED IN THE WASHBASIN.";
 if(s->stage==8)return "A THIN SKIN OF ICE OVER THE WATER.";
 if(s->stage==9 || s->stage==10)return "I BROKE IT WITH A FINGER.";
 if(s->stage==11 || s->stage==12)return "I DRANK FROM UNDERNEATH.";
 return "OUTSIDE, THE HIGHER SLOPES HAD TAKEN SNOW.";
}
static ht_joint ht_shelter_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_shelter_mix_pose(ht_person_pose a,const ht_person_pose *b,unsigned q){
 a.hip=ht_shelter_mix(a.hip,b->hip,q);a.shoulder=ht_shelter_mix(a.shoulder,b->shoulder,q);
 for(int i=0;i<2;++i){a.foot[i]=ht_shelter_mix(a.foot[i],b->foot[i],q);a.hand[i]=ht_shelter_mix(a.hand[i],b->hand[i],q);}
 return a;
}
static ht_person_pose ht_shelter_standing(const ht_game *g,int wx,unsigned stride){
 ht_game v=*g;v.x=wx*256;v.y=ht_surface_at(&v,0,wx)*256;v.vx=stride?232:0;v.vy=0;v.grounded=true;v.landing_ticks=0;v.facing=1;v.stride=stride;memset(&v.traversal,0,sizeof(v.traversal));
 return ht_person_pose_at(wx-v.camera/256,v.y/256-v.camera_y/256,&v);
}
static ht_person_pose ht_shelter_pose(const ht_game *g,const ht_shelter_state *s){
 int cx=g->camera/256,cy=g->camera_y/256,by=ht_shelter_bunk_top()-cy;
 ht_person_pose start=ht_shelter_standing(g,g->x/256,0),sit=ht_shelter_standing(g,213,0),rest=sit;
 sit.hip=ht_joint_at(210-cx,by-3);sit.shoulder=ht_joint_at(209-cx,by-12);
 for(int i=0;i<2;++i){sit.foot[i]=ht_joint_at(214+i*2-cx,ht_surface_at(g,0,214+i*2)-cy);sit.hand[i]=ht_joint_at(207+i*5-cx,by-4);}
 rest.hip=ht_joint_at(197-cx,by-3);rest.shoulder=ht_joint_at(186-cx,by-4);
 rest.foot[0]=ht_joint_at(209-cx,by);rest.foot[1]=ht_joint_at(211-cx,by);
 rest.hand[0]=ht_joint_at(183-cx,by-7);rest.hand[1]=ht_joint_at(189-cx,by-9);rest.facing=-1;
 if(!s->stage)return start;
 if(s->stage==1){unsigned t=ht_min(s->tick,160);return t<80?ht_shelter_mix_pose(start,&sit,t*256/80):ht_shelter_mix_pose(sit,&rest,(t-80)*256/80);}
 if(s->stage<=6){
  if(s->stage==5 && s->tick<96){int turn=ht_min(s->tick,96-s->tick);rest.hip.y-=turn/24;rest.shoulder.y-=turn/32;rest.hand[0].y-=turn/32;rest.hand[1].y-=turn/32;}
  return rest;
 }
 int target=HT_SHELTER_BASIN_X-7;
 ht_person_pose stand=ht_shelter_standing(g,213,0),basin=ht_shelter_standing(g,target,0);
 ht_joint contact=ht_shelter_basin();contact.x-=cx;contact.y-=cy;
 basin.hand[1]=contact;
 if(s->stage==7){unsigned t=ht_min(s->tick,128);if(t<48)return ht_shelter_mix_pose(rest,&sit,t*256/48);if(t<72)return ht_shelter_mix_pose(sit,&stand,(t-48)*256/24);int wx=213+(target-213)*(int)(t-72)/56;ht_person_pose p=ht_shelter_standing(g,wx,t*96);p.hand[1]=ht_shelter_mix(p.hand[1],contact,(unsigned)ht_clamp((int)t-112,0,16)*16);return p;}
 if(s->stage<=12){
  if(s->stage==11 || s->stage==12){ht_joint mouth=ht_joint_at(basin.shoulder.x+3,basin.shoulder.y-3);unsigned q=s->stage==12?256:ht_min(s->tick,80)*256/80;basin.hand[1]=ht_shelter_mix(contact,mouth,q);}
  return basin;
 }
 unsigned t=s->stage==14?160:ht_min(s->tick,160);
 if(t<32){ht_person_pose end=ht_shelter_standing(g,target,0);basin.hand[1]=ht_joint_at(basin.shoulder.x+3,basin.shoulder.y-3);return ht_shelter_mix_pose(basin,&end,t*8);}
 int wx=target+(HT_SHELTER_DOOR_X+18-target)*(int)(t-32)/128;
 return ht_shelter_standing(g,wx,s->stage==13 && t<160?t*96:0);
}
/* The head follows the torso onto the bunk. Damp clothes remain the darkest
 * material, while the stockings extend beyond the actual hand endpoints. */
static void ht_shelter_body(const ht_person_pose *p,bool stockings){
 ht_person_leg(p->hip,p->foot[0],-1,p->facing,237);ht_person_arm(p->shoulder,p->hand[0],-1,239);
 ht_limb(ht_scene,p->shoulder.x,p->shoulder.y,p->hip.x,p->hip.y,3,3,253);
 int dx=p->shoulder.x-p->hip.x,dy=p->shoulder.y-p->hip.y,d=ht_max(1,(int)ht_isqrt((unsigned)(dx*dx+dy*dy)));
 ht_joint head=ht_joint_at(p->shoulder.x+dx*5/d,p->shoulder.y+dy*5/d);
 ht_limb(ht_scene,p->shoulder.x,p->shoulder.y,head.x,head.y,1,1,243);ht_disk(ht_scene,head.x,head.y,3,250);
 ht_person_leg(p->hip,p->foot[1],1,p->facing,251);ht_person_arm(p->shoulder,p->hand[1],1,250);
 if(stockings)for(int i=0;i<2;++i){ht_joint h=p->hand[i];ht_limb(ht_scene,h.x+1,h.y+2,h.x-4,h.y-3,2,2,116);ht_limb(ht_scene,h.x-4,h.y-3,h.x-6,h.y-2,2,1,122);}
}
/* Close view of the same two hands. Cloth is drawn from its cuff toward the
 * fingers, then hangs beyond them. The second hand is dressed only after the
 * first; the loose toe always follows that hand rather than a screen marker. */
static ht_joint ht_shelter_stocking_target(unsigned side){return ht_joint_at(side?299:176,122);}
static unsigned ht_shelter_stocking_cover(const ht_shelter_state *s,unsigned side){return s->stage>3?256:(unsigned)ht_clamp((int)s->tick-(side?80:16),0,32)*8;}
static ht_joint ht_shelter_stocking_cuff(const ht_shelter_state *s,unsigned side){ht_joint h=ht_shelter_stocking_target(side);return ht_joint_at(h.x+(side?-11:12),h.y-22+(int)ht_shelter_stocking_cover(s,side)*56/256);}
static ht_joint ht_shelter_stocking_hand(const ht_shelter_state *s,unsigned side){
 ht_joint base=ht_shelter_stocking_target(side);if(s->stage!=3)return base;
 unsigned target=s->tick<64?0:1;if(side==target)return base;
 unsigned t=s->tick%64;ht_joint cuff=ht_shelter_stocking_cuff(s,target);ht_joint grip=ht_joint_at(cuff.x+6,cuff.y+13);
 unsigned q=t<16?t*16:t<48?256:(64-t)*16;return ht_shelter_mix(base,grip,q);
}
static void ht_shelter_stocking_insert(const ht_shelter_state *s){
 ht_rect(ht_scene,0,0,480,226,151);ht_rect(ht_scene,40,187,405,11,207);
 /* Draw the receiving hand first so the pulling fingertips meet its cuff. */
 unsigned first=s->stage==3 && s->tick>=64?1:0;
 for(unsigned n=0;n<2;++n){unsigned i=n?1-first:first;ht_joint h=ht_shelter_stocking_hand(s,i);unsigned q=ht_shelter_stocking_cover(s,i);
  ht_limb(ht_scene,i?370:105,226,h.x,h.y+30,18,11,252);ht_limb(ht_scene,h.x,h.y+30,h.x,h.y,9,7,203);
  for(int finger=0;finger<4;++finger)ht_limb(ht_scene,h.x-6+finger*4,h.y+3,h.x-6+finger*4,h.y-13-(finger%2)*3,2,1,211);
  if(q){int top=h.y-36,cuff=h.y-22+(int)q*56/256;ht_rect(ht_scene,h.x-11,top,23,cuff-top,105);
   ht_limb(ht_scene,h.x-11,cuff,h.x+12,cuff,2,2,147);
   for(int rib=0;rib<5;++rib)ht_limb(ht_scene,h.x-8+rib*4,top+2,h.x-8+rib*4,cuff-3,0,0,135);
   ht_limb(ht_scene,h.x,top,h.x+(i?9:-9),top-8,8,5,105);ht_limb(ht_scene,h.x+(i?9:-9),top-8,h.x+(i?14:-14),top-3,5,3,108);
   if(s->stage==5 && s->tick>=96)for(int finger=0;finger<4;++finger)ht_disk(ht_scene,h.x-7+finger*5,h.y-4+finger%2*3,2,188);
  }
  if(s->stage==3 && i!=first && s->tick%64>=16 && s->tick%64<48){
   /* The receiving cuff meets a fingertip, including the cloth-covered
    * pinch when the already dressed hand pulls on the second stocking. */
   ht_limb(ht_scene,h.x-6,h.y-13,h.x-2,h.y-11,2,1,q?147:211);
  }
 }
 ht_checkpoint();
}
static bool ht_shelter_ice_broken(const ht_shelter_state *s){return s->stage>9 || (s->stage==9 && s->tick>=32);}
static ht_joint ht_shelter_ice_contact(void){return ht_joint_at(247,116);}
static ht_joint ht_shelter_ice_hand(const ht_shelter_state *s){
 ht_joint contact=ht_shelter_ice_contact(),away=ht_joint_at(312,41),mouth=ht_joint_at(323,44);
 if(s->stage<9)return away;
 if(s->stage==9)return ht_shelter_mix(away,contact,ht_min(s->tick,32)*8);
 if(s->stage==11 || s->stage==12)return ht_shelter_mix(contact,mouth,s->stage==12?256:ht_min(s->tick,80)*256/80);
 return contact;
}
static void ht_shelter_basin_insert(const ht_shelter_state *s){
 ht_rect(ht_scene,0,0,480,226,125);ht_rect(ht_scene,46,169,388,17,175);
 ht_triangle(ht_scene,75,89,405,89,366,204,180);ht_triangle(ht_scene,75,89,366,204,109,204,199);
 ht_triangle(ht_scene,84,88,398,88,362,151,80);ht_triangle(ht_scene,84,88,362,151,116,151,80);
 ht_limb(ht_scene,76,88,404,88,4,4,223);ht_limb(ht_scene,76,88,109,153,4,4,212);ht_limb(ht_scene,109,153,369,153,4,4,232);ht_limb(ht_scene,369,153,404,88,4,4,224);
 bool broken=ht_shelter_ice_broken(s);ht_joint c=ht_shelter_ice_contact();
 if(!broken){ht_triangle(ht_scene,94,94,387,94,356,145,42);ht_triangle(ht_scene,94,94,356,145,123,145,42);ht_limb(ht_scene,119,106,301,103,0,0,85);}
 else{
  static const int16_t edge[][2]={{94,94},{211,96},{387,94},{366,133},{351,145},{122,145}};
  for(unsigned n=0;n<6;++n){unsigned next=(n+1)%6;int ax=edge[n][0],ay=edge[n][1],bx=edge[next][0],byy=edge[next][1];int mx=(ax+bx+c.x)/3,my=(ay+byy+c.y)/3;ht_triangle(ht_scene,ax,ay,bx,byy,mx,my,43);ht_limb(ht_scene,mx,my,bx,byy,0,0,108);}
  ht_limb(ht_scene,c.x-12,c.y+12,c.x+15,c.y+10,0,0,158);
 }
 if(s->stage==11 || s->stage==12){
  /* The raised palm meets this mouth, rather than disappearing off-screen. */
  ht_limb(ht_scene,363,24,397,1,13,18,223);ht_disk(ht_scene,347,23,22,181);ht_disk(ht_scene,354,9,20,249);
  ht_triangle(ht_scene,333,23,323,35,335,39,181);ht_limb(ht_scene,323,44,334,44,1,1,218);ht_limb(ht_scene,334,45,341,52,3,4,181);
 }
 if(s->stage>=9){ht_joint h=ht_shelter_ice_hand(s);ht_limb(ht_scene,426,0,h.x+26,h.y-30,17,9,250);ht_limb(ht_scene,h.x+26,h.y-30,h.x+4,h.y-10,8,4,215);ht_limb(ht_scene,h.x+4,h.y-10,h.x,h.y,3,1,220);
  if(s->stage==11 || s->stage==12){ht_limb(ht_scene,h.x-9,h.y+2,h.x+5,h.y+3,3,3,215);ht_limb(ht_scene,h.x-7,h.y,h.x+2,h.y+1,1,1,77);if(s->stage==11 && s->tick<70)for(int n=0;n<3;++n)ht_rect(ht_scene,h.x-3+n*4,h.y+10+n*9,1,2,105);}
 }
 ht_checkpoint();
}
static void ht_shelter_study_render(const ht_game *game,const ht_shelter_state *s){
 ht_game v=*game;v.camera=(HT_SHELTER_X+16-240)*256;v.camera_y=(ht_shelter_floor()-177)*256;
 v.intimacy=ht_camera_mode==HT_CAMERA_NO_ZOOM || ht_camera_mode==HT_CAMERA_OFF?0:256;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 bool ice=s->stage>=6 || (s->stage==5 && s->tick>=192);
 if(ice){ht_joint b=ht_shelter_basin();int x=b.x-v.camera/256,y=b.y-v.camera_y/256;ht_limb(ht_scene,x-9,y-1,x+9,y-1,0,0,ht_shelter_ice_broken(s)?94:35);}
 /* Dawn enters through the existing doorway. No live weather/clock changes. */
 unsigned dawn=s->stage>=6?256:s->stage==5?(unsigned)ht_clamp((int)s->tick-192,0,128)*2:0;
 if(dawn){int x=HT_SHELTER_DOOR_X-v.camera/256,f=ht_surface_at(&v,0,HT_SHELTER_DOOR_X)-v.camera_y/256;ht_triangle(ht_scene,x-10,f-63,x+15,f-63,x+15,f,140-(int)dawn*69/256);ht_triangle(ht_scene,x-10,f-63,x+15,f,x-10,f,140-(int)dawn*69/256);ht_limb(ht_scene,x+7,f,x-7,f+2,2,2,105);}
 ht_person_pose p=ht_shelter_pose(&v,s);ht_shelter_body(&p,s->stage>=4 && s->stage<=6);
 ht_world_scale=256;
 /* A fixed, 320-step cutscene: painful turn, marked cloth, dawn ice. */
 bool cloth=s->stage==3 || s->stage==4 || (s->stage==5 && s->tick>=96 && s->tick<192);
 bool basin=s->stage>=8 && s->stage<=12;
 if(cloth)ht_shelter_stocking_insert(s);
 else if(basin)ht_shelter_basin_insert(s);
 else if(s->stage>=2 && s->stage<=5){unsigned dark=s->stage<5?66:s->tick<192?66:66-(unsigned)ht_clamp((int)s->tick-192,0,128)*66/128;
  int w=ht_native_active?HT_NATIVE_W:HT_W,h=ht_native_active?HT_NATIVE_H:HT_H;
  for(int y=0;y<h;++y){for(int x=0;x<w;++x){unsigned at=(unsigned)y*w+x;ht_scene[at]=(uint8_t)(ht_scene[at]+(255-ht_scene[at])*dark/128);}if(!(y%32))ht_checkpoint();}
 }
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_shelter_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A LIE DOWN  START JOURNAL  X RETURN":s->stage==2?"A PUT ON STOCKINGS  START JOURNAL  X RETURN":s->stage==4?"A REST UNTIL DAWN  START JOURNAL  X RETURN":s->stage==6?"A GO TO BASIN  START JOURNAL  X RETURN":s->stage==8?"A BREAK ICE  START JOURNAL  X RETURN":s->stage==10?"A DRINK  START JOURNAL  X RETURN":s->stage==12?"A LEAVE SHELTER  START JOURNAL  X RETURN":s->stage==14?"A CONTINUE  START JOURNAL  X RETURN":"PAUSE HOLD  START JOURNAL  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
static void ht_shelter_prompt(const ht_game *g){if(!ht_shelter_near(g))return;ht_rect(ht_scene,124,221,239,15,255);ht_text_color(134,225,"A REST IN THE MAINTENANCE SHELTER",1,85);}
