#define HT_POUCH_STUDY 1
typedef struct {bool active,paused;int8_t direction;uint8_t stage,turn;uint16_t tick;} ht_pouch_state;
static ht_pouch_state ht_pouch;
static bool ht_pouch_near(const ht_game *g){return g->level==5 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==0;}
static void ht_pouch_begin(void){ht_pouch=(ht_pouch_state){true,false,0,0,0,0};}
static bool ht_pouch_step(ht_pouch_state *s){
 if(!s->active || s->paused)return false;
 if(s->stage==1){if(++s->tick==96){s->stage=2;s->tick=0;}return true;}
 if(s->stage==2){int old=s->turn;s->turn=(uint8_t)ht_clamp(old+s->direction,0,64);return old!=s->turn;}
 if(s->stage==3){if(++s->tick==144){s->stage=4;s->tick=144;}return true;}
 return false;
}
static bool ht_pouch_action(ht_pouch_state *s){
 if(!s->active || s->paused || (s->stage!=0 && s->stage!=2))return false;
 s->stage=s->stage==0?1:3;s->tick=0;s->direction=0;return true;
}
static const char *ht_pouch_label(const ht_pouch_state *s){
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->stage==0)return "A CHILD'S POUCH LAY BENEATH A SLAB.";
 if(s->stage==1)return "I SET SIX STONES IN A SMALL, UNEVEN LINE.";
 if(s->stage==2)return s->turn<32?"HELD ONE WAY, THE CHARCOAL FACE SMILED.":"TURNED OVER, IT SEEMED TO FROWN.";
 if(s->stage==3)return "THE ONE WITH THE FACE WENT IN LAST.";
 return "ITS EXPRESSION CHANGED BETWEEN MY FINGERS.";
}
static ht_joint ht_pouch_rot(int x,int y,unsigned turn){
 static const int16_t cosine[17]={256,251,237,213,181,142,98,50,0,-50,-98,-142,-181,-213,-237,-251,-256};
 static const int16_t sine[17]={0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,0};
 unsigned i=ht_min(turn,64)/4,j=ht_min(i+1,16),a=turn%4;
 int c=(cosine[i]*(int)(4-a)+cosine[j]*(int)a)/4;
 int s=(sine[i]*(int)(4-a)+sine[j]*(int)a)/4;
 return ht_joint_at((x*c-y*s)/256,(x*s+y*c)/256);
}
static void ht_pouch_stone(int x,int y,int radius,unsigned turn,bool face){
 static const int8_t edge[9][2]={{-9,-3},{-5,-8},{3,-9},{8,-5},{10,2},{6,8},{-2,9},{-8,5},{-9,-3}};
 for(int n=0;n<8;++n){
  ht_joint a=ht_pouch_rot(edge[n][0],edge[n][1],face?turn:0),b=ht_pouch_rot(edge[n+1][0],edge[n+1][1],face?turn:0);
  ht_triangle(ht_scene,x-1,y-1,x+a.x*radius/10,y+a.y*radius/10,x+b.x*radius/10,y+b.y*radius/10,n<4?72:106);
  ht_limb(ht_scene,x+a.x*radius/10,y+a.y*radius/10,x+b.x*radius/10,y+b.y*radius/10,1,1,174);
 }
 ht_limb(ht_scene,x-radius/2,y+radius*3/4,x+radius/3,y+radius*3/4,1,0,153);
 if(!face){ht_limb(ht_scene,x-radius/2,y-radius/3,x-radius/4,y-radius/2,1,0,134);return;}
 /* The chip and charcoal turn together; the expression is never swapped. */
 ht_joint chip=ht_pouch_rot(0,6,turn);
 ht_disk(ht_scene,x+chip.x*radius/10,y+chip.y*radius/10,ht_max(1,radius/5),168);
 for(int side=-1;side<=1;side+=2){ht_joint e=ht_pouch_rot(side*4,-3,turn);ht_disk(ht_scene,x+e.x*radius/10,y+e.y*radius/10,ht_max(1,radius/10),230);}
 ht_joint old=ht_pouch_rot(-5,2,turn);
 for(int n=-4;n<=5;++n){ht_joint at=ht_pouch_rot(n,6-n*n/6,turn);ht_limb(ht_scene,x+old.x*radius/10,y+old.y*radius/10,x+at.x*radius/10,y+at.y*radius/10,1,1,234);old=at;}
}
static void ht_pouch_study_render(const ht_game *game,const ht_pouch_state *s){
 ht_game v=*game;int wx=ht_evidence_x(5,0),floor=ht_surface_at(game,1,wx);
 v.camera=(wx-220)*256;v.camera_y=(floor-190)*256;v.intimacy=128;v.vista=v.drop_zoom=0;v.rotation_phase=v.sway_phase=0;
 ht_frame_camera(&v,ht_camera_mode==HT_CAMERA_NATIVE?(unsigned)HT_CAMERA_BASELINE:ht_camera_mode);
 ht_render_scene_from(&v,false);ht_world_scale=ht_scene_scale(&v);
 if(!s->stage){
  v.x=(wx-13)*256;v.y=ht_surface_at(&v,1,wx-13)*256;v.vx=v.vy=0;v.facing=1;v.ticks=0;
  ht_person_pose p=ht_person_pose_at(v.x/256-v.camera/256,(v.y-v.camera_y)/256,&v);ht_person_draw(&p);
 }else{
  ht_world_scale=256;
  /* Close view of the same slab: coarse fractures and the report remain. */
  ht_triangle(ht_scene,42,130,405,117,445,212,139);ht_triangle(ht_scene,42,130,445,212,29,222,163);
  for(int n=0;n<9;++n)ht_limb(ht_scene,55+n*39,139+n%3*8,69+n*38,148+n%3*11,1,0,112);
  for(int n=0;n<110;++n){int x=48+(int)(ht_hash(n+710)%364),y=150+(int)(ht_hash(n+301)%55);ht_rect(ht_scene,x,y,1+(n%3),1,n%2?148:157);}
  ht_triangle(ht_scene,326,173,389,177,388,204,72);ht_triangle(ht_scene,326,173,388,204,322,201,72);
  for(int n=0;n<3;++n)ht_rect(ht_scene,332,180+n*6,43-n*7,1,144);
  ht_triangle(ht_scene,356,133,400,137,409,186,211);ht_triangle(ht_scene,356,133,409,186,345,183,198);
  ht_disk(ht_scene,377,142,18,224);ht_limb(ht_scene,359,141,395,143,2,1,237);
  unsigned placed=s->stage==1?ht_min(6,s->tick/16):6;
  unsigned returned=s->stage==3?ht_min(6,s->tick/24):s->stage==4?6:0;
  for(unsigned n=0;n<6;++n){
   bool marked=n==0;unsigned order=marked?5:n-1;
   if(n>=placed || order<returned || (s->stage==3 && order==returned))continue;
   int x=86+(int)n*43,y=163+(n%3)*4;
   bool held=s->stage==2 && marked;
   if(held){x=155;y=108;}
   ht_pouch_stone(x,y,held?23:12,marked?s->turn:0,marked);
   if(held){
    ht_limb(ht_scene,124,218,133,137,8,5,235);ht_limb(ht_scene,133,137,138,116,4,2,240);
    ht_limb(ht_scene,203,218,181,137,8,5,237);ht_limb(ht_scene,181,137,175,110,4,2,242);
   }
  }
  /* One hand places/takes each stone; marked stone is returned sixth. */
  if(s->stage==1 || s->stage==3){
   unsigned interval=s->stage==1?16:24,within=s->tick%interval;
   unsigned index=s->stage==1?ht_min(5,s->tick/interval):ht_min(5,s->tick/interval)+1;
   if(index==6)index=0;
   int stone=86+(int)index*43,progress=(int)within*256/(int)interval;
   int x=s->stage==1?377+(stone-377)*progress/256:stone+(377-stone)*progress/256;
   int y=151-(int)ht_min(within,interval-within)*2;
   ht_limb(ht_scene,x-28,219,x,y+12,6,4,237);ht_disk(ht_scene,x,y+8,4,241);
   ht_pouch_stone(x,y,10,index==0?s->turn:0,index==0);
  }
 }
 ht_world_scale=256;ht_rect(ht_scene,8,226,464,35,255);
 const char *label=ht_pouch_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,65);
 const char *keys=s->stage==0?"A TAKE POUCH  START NOTE  X RETURN":s->stage==2?"L/R TURN STONE  A PUT BACK  X RETURN":s->stage==4?"A READ NOTE  START NOTE  X RETURN":"START NOTE  PAUSE HOLD  X RETURN";
 ht_text_color((HT_W-(int)strlen(keys)*6)/2,244,keys,1,103);ht_vignette();
}
