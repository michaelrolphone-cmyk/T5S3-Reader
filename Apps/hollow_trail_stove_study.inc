/* An input-owned physical inspection. Tick progression never changes gameplay. */
typedef struct { bool active,paused; uint8_t stage; uint16_t tick; } ht_stove_state;
static ht_stove_state ht_stove;
static bool ht_stove_near(const ht_game *g) {
 return g->level==2 && g->grounded && g->traversal.mode==HT_FREE &&
        ht_evidence_near(g)==1;
}
static void ht_stove_begin(void) {ht_stove=(ht_stove_state){true,false,0,0};}
static void ht_stove_step(ht_stove_state *s) {
 if(!s->active || s->paused)return;
 if(s->stage==0 && s->tick<192) {if(++s->tick==192){s->stage=1;s->tick=0;}}
 else if(s->stage==2 && s->tick<144) {if(++s->tick==144){s->stage=3;s->tick=0;}}
 else if(s->stage==3 && s->tick<320)++s->tick; /* bounded unanswered hold */
}
static bool ht_stove_reply(ht_stove_state *s) {
 if(!s->active || s->paused || s->stage!=1)return false;
 s->stage=2;s->tick=0;return true;
}
static unsigned ht_stove_pulse(const ht_stove_state *s,unsigned *age) {
 static const unsigned incoming[3]={40,78,116};
 if(s->stage==0)for(int i=2;i>=0;--i)if(s->tick>=incoming[i] && s->tick<incoming[i]+24) {
  *age=s->tick-incoming[i];return (unsigned)i+1;
 }
 *age=0;return 0;
}
static const char *ht_stove_caption(const ht_stove_state *s) {
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(s->stage==0) {
  if(s->tick<40)return "THE CLOTH IS STIFF WITH SOOT.";
  if(s->tick<78)return "A KNOCK BENEATH THE FLOOR.";
  if(s->tick<116)return "A SECOND KNOCK.";
  if(s->tick<155)return "THEN A LONGER SHUDDER.";
  return "THE DOORWAY'S REFLECTION RETURNED.";
 }
 if(s->stage==1)return "NOTHING. A - REPLY WITH THE KNIFE HANDLE";
 if(s->stage==2)return "THE FIRST BLOW WAS TOO LIGHT. I TRIED AGAIN.";
 return s->tick<320?"I WAITED FOR AN ANSWER.":"THE PIPE DID NOT KNOCK AGAIN. A - READ NOTE";
}
static void ht_stove_study_render(const ht_game *game,const ht_stove_state *s) {
 ht_native_active=ht_camera_mode==HT_CAMERA_NATIVE && ht_native_a;
 ht_native_foreground_half_y=false;ht_scene=ht_native_active?ht_native_a:ht_scene_low;
 ht_world_scale=256;
 for(int y=0;y<HT_H;++y){ht_rect(ht_scene,0,y,HT_W,1,105+y/8);if((y&15)==15)ht_checkpoint();}
 ht_game v=*game;v.camera=(HT_STOVE_X-228)*256;v.camera_y=(ht_stove_floor()-191)*256;
 ht_world_scale=384;
 unsigned age=0,pulse=ht_stove_pulse(s,&age);ht_stove_room(&v,pulse,age);
 /* Approach the wrapped pipe in the view, without moving the frozen body.
  * Hands use reachable contacts; feet remain on the room's real floor. */
 int target=HT_STOVE_X-9,start=game->x/256;
 unsigned approach=s->stage==0?ht_min(s->tick,32):32;
 v.x=(start+(target-start)*(int)approach/32)*256;v.y=ht_stove_floor()*256;v.facing=1;
 v.vx=v.vy=0;v.ticks=0;v.grounded=true;
 ht_person_pose p=ht_person_pose_at(v.x/256-v.camera/256,191,&v);
 if(approach==32) {
  int pipe=HT_STOVE_X-v.camera/256;
  if(s->stage==0){p.hand[0]=ht_joint_at(pipe,191-26);p.elbow[0]=1;}
  else if(s->stage==1) {p.shoulder.x=pipe-4;p.shoulder.y=191-21;
   p.hand[0]=ht_joint_at(pipe,191-24);p.elbow[0]=1;}
  else if(s->stage==2) {
   /* One weak blow, then a deliberate three; no responding water rings. */
   unsigned at=s->tick;int d=12;
   static const unsigned strikes[4]={16,48,76,104};
   for(int i=0;i<4;++i)if(at>=strikes[i] && at<strikes[i]+12)d=2+(int)(at-strikes[i]);
   p.hand[0]=ht_joint_at(pipe-d,191-24);p.elbow[0]=1;
   ht_limb(ht_scene,p.hand[0].x,191-24,p.hand[0].x+3,191-24,1,0,187);
  }
 }
 ht_person_draw(&p);
 ht_world_scale=256;
 ht_rect(ht_scene,18,226,444,34,255);
 const char *caption=ht_stove_caption(s);
 ht_text_color((HT_W-(int)strlen(caption)*6)/2,231,caption,1,68);
 ht_text_color(36,246,"START NOTE   X/BACK RETURN   PAUSE HOLD",1,107);
 ht_vignette();
}
