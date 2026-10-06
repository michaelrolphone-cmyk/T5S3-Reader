/* The original discovery owns page 21. This bounded optional examination
 * moves only a render copy; it cannot operate the distributor or the feed. */
#define HT_GAUGE_STUDY 1
typedef struct {bool active,paused;uint8_t stage;uint16_t tick;} ht_gauge_state;
static ht_gauge_state ht_gauge;
static bool ht_gauge_near(const ht_game *g){return g->level==7 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==0;}
static void ht_gauge_begin(void){ht_gauge=(ht_gauge_state){true,false,0,0};}
static unsigned ht_gauge_limit(unsigned stage){return stage==1?64:stage==3 || stage==5?96:stage==7?144:0;}
static bool ht_gauge_step(ht_gauge_state *s){
 if(!s->active || s->paused || !ht_gauge_limit(s->stage))return false;
 if(++s->tick==ht_gauge_limit(s->stage)){++s->stage;s->tick=0;}return true;
}
static bool ht_gauge_action(ht_gauge_state *s){if(!s->active || s->paused || (s->stage&1) || s->stage>=8)return false;++s->stage;s->tick=0;return true;}
static const char *ht_gauge_label(const ht_gauge_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->stage)return "AT THE TURBINE SHED, THE GROUND TREMBLED.";
 if(s->stage==1)return "A LAMP BURNED ABOVE THE MICA GAUGE.";
 if(s->stage==2)return "SCRATCHES DOUBLED THE GAUGE NEEDLE.";
 if(s->stage==3)return "I SHIFTED MY HEAD.";
 if(s->stage==4)return "TRIANGLES: COLD START. ROUND MARKS LOWER.";
 if(s->stage==5)return "I HAD TO MOVE TO FINISH READING IT.";
 if(s->stage==6)return "YOU CANNOT ASK PEOPLE TO WAIT IN DARKNESS.";
 if(s->stage==7)return "ONE WIRE LEFT A SWITCH, THEN DIVIDED.";
 return "SHE KNEW THEY SHARED THE FEED.";
}
static ht_joint ht_gauge_mix(ht_joint a,ht_joint b,unsigned q){return ht_joint_at(a.x+(b.x-a.x)*(int)q/256,a.y+(b.y-a.y)*(int)q/256);}
static ht_person_pose ht_gauge_pose(const ht_game *game,const ht_gauge_state *s){
 ht_game v=*game;unsigned q=s->stage==1?ht_min(s->tick,32)*8:s->stage>=2?256:0;
 int wx=game->x/256+(ht_gauge_x()+2-game->x/256)*(int)q/256;
 v.x=wx*256;v.y=ht_surface_at(&v,1,wx)*256;v.vx=v.vy=0;v.facing=1;v.grounded=true;v.landing_ticks=0;v.stride=0;memset(&v.traversal,0,sizeof(v.traversal));
 ht_person_pose p=ht_person_pose_at(wx-v.camera/256,v.y/256-v.camera_y/256,&v);
 if(s->stage){unsigned reach=s->stage==1?(unsigned)ht_clamp((int)s->tick-32,0,32)*8:256;
  ht_joint c=ht_gauge_contact();c.x-=v.camera/256;c.y-=v.camera_y/256;p.hand[1]=ht_gauge_mix(p.hand[1],c,reach);}
 return p;
}
/* The head translates behind the mica. The true needle and scale share one
 * plane; only the nearer scratches shift relative to that fixed reading. */
static unsigned ht_gauge_head(const ht_gauge_state *s){return s->stage==3?ht_min(s->tick,96)*256/96:s->stage>=4?256:0;}
static ht_joint ht_gauge_needle(unsigned head){return ht_joint_at(211-(int)head/32,80);}
static ht_joint ht_gauge_scratch(unsigned head){ht_joint p=ht_gauge_needle(head);p.x+=3+(int)head/16;return p;}
static void ht_gauge_face(const ht_gauge_state *s){
 unsigned head=ht_gauge_head(s);int cx=248-(int)head/32,cy=111;
 ht_rect(ht_scene,0,0,480,226,132);
 ht_limb(ht_scene,36,204,438,204,13,13,198);ht_limb(ht_scene,36,198,438,198,2,2,113);
 ht_limb(ht_scene,cx,170,cx,193,9,9,213);
 ht_disk(ht_scene,cx,cy,91,210);ht_disk(ht_scene,cx,cy,80,89);ht_disk(ht_scene,cx,cy,72,41);
 for(int n=0;n<4;++n){int y=68+n*24;ht_limb(ht_scene,cx+48,y,cx+60,y,1,1,175);}
 ht_triangle(ht_scene,cx+46,58,cx+54,70,cx+38,70,218);
 ht_disk(ht_scene,cx+57,93,5,218);ht_disk(ht_scene,cx+57,93,2,41);
 ht_text_color(350,56,"COLD START",1,226);ht_text_color(350,69,"3 MEASURES",1,226);
 ht_text_color(350,97,"RUNNING",1,226);ht_text_color(350,110,"2 MEASURES",1,226);
 ht_joint needle=ht_gauge_needle(head),scratch=ht_gauge_scratch(head);
 ht_limb(ht_scene,cx,cy,needle.x,needle.y,2,0,230);ht_disk(ht_scene,cx,cy,5,217);
 ht_limb(ht_scene,cx+3+(int)head/16,cy,scratch.x,scratch.y,0,0,145);
 ht_limb(ht_scene,cx-54+(int)head/16,cy+30,cx-15+(int)head/16,cy-49,0,0,96);
 ht_limb(ht_scene,cx+24+(int)head/16,cy+42,cx+40+(int)head/16,cy+8,0,0,103);
 /* Sheet corners and uneven tabs make the replacement material visible. */
 ht_limb(ht_scene,cx-57,cy-54,cx-62,cy+43,0,0,140);
 for(int side=-1;side<=1;side+=2)for(int row=0;row<2;++row){int x=cx+side*59,y=cy-52+row*106;ht_rect(ht_scene,x-6,y-3,13,7,226);ht_disk(ht_scene,x,y,2,116);}
 ht_text_color(28,25,"CLEAR MICA",1,216);ht_limb(ht_scene,88,37,cx-55,cy-45,0,0,188);
 ht_rect(ht_scene,cx-16,0,32,12,37);ht_limb(ht_scene,cx-22,13,cx+22,13,2,2,225);
}
static void ht_gauge_casing(const ht_gauge_state *s){
 ht_rect(ht_scene,0,0,480,226,132);
 unsigned q=s->stage==5?ht_min(s->tick,96)*256/96:256;int shift=(int)q*89/256;
 /* Reading around the curved casing reveals the rest by parallax. */
 int x=228-shift;
 ht_triangle(ht_scene,x+61,-24,478,16,478,220,188);ht_triangle(ht_scene,x+61,-24,478,220,x+62,249,188);
 ht_disk(ht_scene,x,119,140,209);ht_disk(ht_scene,x,119,111,72);ht_disk(ht_scene,x,119,98,42);
 for(int n=0;n<4;++n)ht_limb(ht_scene,x+65,72+n*23,x+77,72+n*23,1,1,190);
 ht_limb(ht_scene,x,119,x-34,85,2,0,224);ht_disk(ht_scene,x,119,5,222);
 ht_text_color(x+99,39,"I RESTORED POWER",1,36);ht_text_color(x+119,57,"FOR THE SIGNAL.",1,36);
 ht_text_color(x+129,86,"IT HELD THROUGH",1,36);ht_text_color(x+135,104,"THE NIGHT.",1,36);
 ht_text_color(x+122,133,"YOU CANNOT ASK PEOPLE",1,36);ht_text_color(x+106,151,"TO WAIT IN DARKNESS.",1,36);
 /* The far side falls behind a wall return. Moving the view brings the
  * final words out from behind this fixed edge rather than changing them. */
 ht_rect(ht_scene,433,0,47,226,138);ht_limb(ht_scene,433,0,433,225,2,2,188);
 ht_limb(ht_scene,410,235,x+127,187,10,6,229);ht_limb(ht_scene,x+127,187,x+112,174,5,2,221);
}
/* Printed route and tracing fingertip use the same segments, first to lamps
 * and back to the junction, then onward to the recorder. The switch stays. */
static const ht_joint ht_gauge_feed_path[]={{125,128},{225,128},{225,69},{365,69},{225,69},{225,128},{225,169},{365,169}};
static ht_joint ht_gauge_trace(unsigned tick){
 unsigned t=ht_min(tick,140),leg=ht_min(t/20,6),q=(t-leg*20)*256/20;
 return ht_gauge_mix(ht_gauge_feed_path[leg],ht_gauge_feed_path[leg+1],q);
}
static void ht_gauge_diagram(const ht_gauge_state *s){
 ht_rect(ht_scene,0,0,480,226,138);ht_rect(ht_scene,70,22,353,188,73);
 for(int n=0;n<4;++n)ht_disk(ht_scene,n%2?415:77,n/2?202:30,3,220);
 ht_text_color(91,35,"COMMON FEED",1,218);
 ht_rect(ht_scene,101,113,24,29,200);ht_limb(ht_scene,111,134,115,122,2,2,84);
 for(unsigned n=0;n<7;++n){ht_joint a=ht_gauge_feed_path[n],b=ht_gauge_feed_path[n+1];ht_limb(ht_scene,a.x,a.y,b.x,b.y,1,1,219);}
 ht_disk(ht_scene,225,128,4,222);ht_text_color(304,52,"LAMPS",1,224);ht_text_color(293,152,"RECORDER",1,224);
 ht_limb(ht_scene,365,69,365,84,1,1,217);ht_triangle(ht_scene,355,86,365,78,375,86,214);ht_disk(ht_scene,365,89,4,37);
 ht_rect(ht_scene,365,174,21,15,214);ht_limb(ht_scene,367,177,377,173,0,0,52);ht_limb(ht_scene,377,173,382,184,0,0,52);
 ht_joint tip=ht_gauge_trace(s->stage==7?s->tick:140);
 ht_limb(ht_scene,431,240,tip.x+34,tip.y+41,11,7,231);ht_limb(ht_scene,tip.x+34,tip.y+41,tip.x+12,tip.y+15,6,4,221);
 ht_limb(ht_scene,tip.x+12,tip.y+15,tip.x,tip.y,3,1,221);
 ht_text_color(28,213,"SHE KNEW THEY SHARED THE FEED.",1,222);
}
static void ht_gauge_study_render(const ht_game *game,const ht_gauge_state *s){
 ht_game v=*game;v.camera=(ht_gauge_x()-162)*256;v.camera_y=(ht_gauge_floor()-192)*256;
 v.intimacy=256;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);ht_render_scene_from(&v,false);
 ht_world_scale=ht_scene_scale(&v);
 if(s->stage<2){ht_person_pose p=ht_gauge_pose(&v,s);ht_person_draw(&p);}
 ht_world_scale=256;
 if(s->stage>=2 && s->stage<=4)ht_gauge_face(s);
 else if(s->stage==5 || s->stage==6)ht_gauge_casing(s);
 else if(s->stage>=7)ht_gauge_diagram(s);
 ht_rect(ht_scene,5,226,470,35,255);const char *label=ht_gauge_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=!s->stage?"A APPROACH  START NOTE  X RETURN":s->stage==2?"A SHIFT HEAD  START NOTE  X RETURN":s->stage==4?"A READ CASING  START NOTE  X RETURN":s->stage==6?"A TRACE FEED  START NOTE  X RETURN":s->stage==8?"A READ NOTE  START NOTE  X RETURN":"PAUSE HOLD  START NOTE  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
