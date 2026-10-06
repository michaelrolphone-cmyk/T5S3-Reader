/* Chapter IV: optional, bounded images within the chosen carriage rest.
 * These are a reflection and a dream, never an encounter or a new find.
 * The face/filled-room motifs are shared with the later garden recollection. */
static void ht_sister_quad(int ax,int ay,int bx,int by,int cx,int cy,int dx,int dy,int ink);
static void ht_sister_face(int x,int y,int young,int crease,int ghost,bool closed,bool lying);
static void ht_sister_face_toward(int x,int y,int young,int crease,int ghost,bool closed,bool lying,int facing);
static int ht_carriage_ease(unsigned t,unsigned start,unsigned end){
 int u=ht_clamp(((int)t-(int)start)*256/(int)(end-start),0,256);return u*u*(768-2*u)/65536;
}
static void ht_carriage_close_room(void){
 ht_rect(ht_scene,0,0,480,226,106);ht_rect(ht_scene,0,182,480,44,155);
 ht_rect(ht_scene,28,27,421,120,184);ht_rect(ht_scene,40,37,185,97,40);ht_rect(ht_scene,244,37,193,97,45);
 /* The divided sky lies behind the glass; it does not become a signal. */
 ht_limb(ht_scene,47,74,425,74,2,2,96);ht_limb(ht_scene,113,38,113,130,2,2,111);
 ht_limb(ht_scene,343,38,343,130,2,2,113);ht_limb(ht_scene,113,75,146,39,1,1,92);
 ht_limb(ht_scene,343,75,379,39,1,1,92);
 ht_limb(ht_scene,235,28,235,146,4,4,226);ht_limb(ht_scene,30,147,447,147,3,3,219);
 /* Hull-like ribs, rack and the two loose tongues keep the insert in this car. */
 for(int n=0;n<5;++n){int x=18+n*102;ht_limb(ht_scene,x,0,x+34,20,3,2,212);ht_limb(ht_scene,x+34,20,x+53,31,2,1,201);}
 ht_limb(ht_scene,21,25,458,25,3,3,215);ht_limb(ht_scene,406,25,404,59,2,2,197);
 ht_limb(ht_scene,404,59,396,76,2,1,202);ht_limb(ht_scene,404,59,412,73,2,1,202);
 ht_rect(ht_scene,75,153,342,26,155);ht_rect(ht_scene,72,180,348,19,207);
 ht_limb(ht_scene,75,200,417,200,3,3,236);ht_limb(ht_scene,90,203,90,226,3,3,228);ht_limb(ht_scene,399,203,399,226,3,3,228);
 for(int n=0;n<7;++n){int x=286+n*17;ht_limb(ht_scene,x,155,x-3,172+(n%3),2,1,182);}
 for(int n=0;n<8;++n)ht_limb(ht_scene,40+n*53,204,17+n*61,226,0,0,184);
}
/* One reclining shape supplies both the body and its horizontally lying
 * reflection. Window coordinates keep every reflected pixel inside the pane. */
static void ht_carriage_reclining(int x,int y,int scale,int ink,bool turned){
#define RX(a) (x+(a)*scale/256)
#define RY(a) (y+(a)*scale/256)
 ht_sister_quad(RX(43),RY(-16),RX(120),RY(-8),RX(157),RY(7),RX(29),RY(9),ink);
 ht_limb(ht_scene,RX(46),RY(-8),RX(92),RY(6),8*scale/256,9*scale/256,ink);
 ht_disk(ht_scene,RX(22),RY(-9),12*scale/256,ink);
 ht_disk(ht_scene,RX(16),RY(-13),9*scale/256,ink+12);
 if(!turned){
  ht_triangle(ht_scene,RX(26),RY(-14),RX(39),RY(-5),RX(29),RY(0),ink-29);
  ht_limb(ht_scene,RX(29),RY(-4),RX(46),RY(6),3*scale/256,3*scale/256,ink-24);
 }else ht_limb(ht_scene,RX(20),RY(-21),RX(33),RY(-9),6*scale/256,6*scale/256,ink+12);
#undef RY
#undef RX
}
static void ht_carriage_reflection_draw(unsigned t){
 ht_carriage_close_room();
 bool turned=t>=224;
 ht_carriage_reclining(57,115,232,87,turned);
 /* The reflected bench, dim diagonal marks and jamb remain over the image. */
 ht_limb(ht_scene,49,129,218,129,1,1,108);ht_limb(ht_scene,54,41,108,119,0,0,62);ht_limb(ht_scene,155,41,210,121,0,0,64);
 ht_carriage_reclining(82,175,430,231,turned);
 /* Boots still point into the foreground aisle below the dry bench. */
 ht_limb(ht_scene,132,207,132,216,4,4,239);ht_limb(ht_scene,132,216,116,216,4,4,239);
 ht_limb(ht_scene,163,207,163,218,4,4,239);ht_limb(ht_scene,163,218,148,218,4,4,239);
 ht_checkpoint();
}
static ht_joint ht_carriage_blank_place(void){return ht_joint_at(223,184);}
static ht_joint ht_carriage_account_grip(void){return ht_joint_at(103,195);}
static ht_joint ht_carriage_dream_finger(unsigned t){
 ht_joint at=ht_carriage_blank_place();int q=ht_carriage_ease(t,528,608);
 return ht_joint_at(286+(at.x-286)*q/256,171+(at.y-171)*q/256);
}
static void ht_carriage_account(int dx,int dy){
 ht_sister_quad(97+dx,140+dy,252+dx,153+dy,269+dx,214+dy,80+dx,207+dy,31);
 ht_limb(ht_scene,97+dx,140+dy,252+dx,153+dy,0,0,137);
 for(int row=0;row<5;++row){int y=154+row*10+dy;ht_limb(ht_scene,98+dx,y,245+dx,y+7,1,1,103);}
 ht_limb(ht_scene,174+dx,151+dy,175+dx,204+dy,0,0,174);ht_limb(ht_scene,203+dx,153+dy,206+dx,206+dy,0,0,174);
 for(int row=0;row<4;++row){int y=156+row*10+dy;
  ht_limb(ht_scene,108+dx,y,151-(row%2)*13+dx,y+2,1,1,181);
  ht_limb(ht_scene,182+dx,y+4,190+dx,y+4,1,1,200);
  /* No marks occupy the place the dream finger will indicate. */
  if(row!=2)ht_limb(ht_scene,215+dx,y+5,235+dx,y+6,1,1,200);
 }
}
static void ht_carriage_dream_draw(unsigned t){
 ht_carriage_close_room();
 /* Indoor dress, bent knees and bare feet resting on the seat. */
 ht_sister_quad(278,130,328,126,340,177,265,177,236);
 ht_limb(ht_scene,284,174,337,153,12,12,224);ht_limb(ht_scene,337,153,369,177,12,7,225);
 ht_limb(ht_scene,306,178,351,160,11,10,243);ht_limb(ht_scene,351,160,385,180,10,6,244);
 ht_limb(ht_scene,369,177,354,184,6,4,155);ht_limb(ht_scene,385,180,370,187,6,4,153);
 for(int n=0;n<3;++n)ht_limb(ht_scene,353+n*3,184,355+n*3,185,0,0,208);
 ht_limb(ht_scene,327,139,345,158,9,6,246);ht_limb(ht_scene,345,158,327,171,6,4,149);
 ht_sister_face_toward(305,88,0,0,0,false,false,-1);
 if(t>=176){
  ht_carriage_account(4,-4);
  /* Three leaves pass through the same supporting hand. All remain accounts. */
  unsigned turn=t<480?(t-176)%104:0;int lift=turn<52?(int)turn:104-(int)turn;
  ht_carriage_account(0,-lift/5);
  ht_joint grip=ht_carriage_account_grip();
  ht_limb(ht_scene,38,225,69,199,12,8,234);ht_limb(ht_scene,69,199,grip.x,grip.y,8,5,151);
  ht_limb(ht_scene,grip.x-8,grip.y-1,grip.x+5,grip.y-2,4,3,151);
 }
 /* The pointing finger and the empty ledger cell use the same endpoint. */
 ht_joint finger=ht_carriage_dream_finger(t);
 ht_limb(ht_scene,278,141,268,163,9,6,222);ht_limb(ht_scene,268,163,finger.x+5,finger.y-1,6,4,149);
 ht_limb(ht_scene,finger.x+5,finger.y-1,finger.x,finger.y,3,1,148);
 ht_checkpoint();
}
