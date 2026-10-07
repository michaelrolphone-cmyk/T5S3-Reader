/* Chapter VII. One bounded recollection earned only by choosing the far pallet
 * and remaining in its held night. Wake/Return can interrupt every frame.
 * The remembered rooms are drawings, never new playable spaces or evidence. */
#define HT_GARDEN_SISTER_MEMORY 1
#define HT_SISTER_MEMORY_START 128u
#define HT_SISTER_MEMORY_END 896u
static bool ht_sister_memory_active(const ht_night_state *s){return s->stage==2 && s->memory_tick>=HT_SISTER_MEMORY_START && s->memory_tick<HT_SISTER_MEMORY_END;}
static unsigned ht_sister_memory_time(const ht_night_state *s){return s->memory_tick-HT_SISTER_MEMORY_START;}
static int ht_sister_ease(unsigned t,unsigned start,unsigned end){int u=ht_clamp(((int)t-(int)start)*256/(int)(end-start),0,256);return u*u*(768-2*u)/65536;}
static const char *ht_sister_memory_label(const ht_night_state *s){
 unsigned t=ht_sister_memory_time(s);
 if(t<176)return "SHE SLEPT WITH ONE HAND FOLDED UNDER HER CHEEK.";
 if(t<368)return "I HAD WATCHED THAT CREASE FADE WHILE SHE MADE TEA.";
 if(t<464)return "THE YOUNGER FACE I REMEMBERED FROM THE GRASS";
 if(t<528)return "AND THE TIRED FACE IN OUR DOORWAY";
 if(t<592)return "HER FACE WOULD NOT HOLD STILL.";
 if(t<688)return "THEN THE FACE IN THE DREAM RETURNED.";
 return "WAITING FOR ME TO SEE WHAT I HAD LEFT OUT.";
}
static void ht_sister_quad(int ax,int ay,int bx,int by,int cx,int cy,int dx,int dy,int ink){ht_triangle(ht_scene,ax,ay,bx,by,cx,cy,ink);ht_triangle(ht_scene,ax,ay,cx,cy,dx,dy,ink);}
static void ht_sister_room(int wall,int floor){
 ht_rect(ht_scene,0,0,480,226,wall);ht_rect(ht_scene,0,176,480,50,floor);
 for(int n=0;n<7;++n)ht_limb(ht_scene,50+n*57,177,15+n*75,225,0,0,floor+18);
}
/* The same asymmetrical profile persists through all three uncertain faces.
 * Youth/tiredness alter the jaw and posture, not the identity of the figure. */
static void ht_sister_face_toward(int x,int y,int young,int crease,int ghost,bool closed,bool lying,int facing){
 int hair=250-ghost,skin=137-ghost/3,edge=222-ghost;
 /* In bed, the same profile turns onto the pillow: neck toward the body,
  * cheek toward the folded hand. No upright head on a horizontal body. */
#define SX(a,b) (x+(lying?(b):(a)*facing))
#define SY(a,b) (y+(lying?(a):(b)))
#define SL(ax,ay,bx,by,ra,rb,ink) ht_limb(ht_scene,SX(ax,ay),SY(ax,ay),SX(bx,by),SY(bx,by),ra,rb,ink)
#define SD(ax,ay,r,ink) ht_disk(ht_scene,SX(ax,ay),SY(ax,ay),r,ink)
#define ST(ax,ay,bx,by,cx,cy,ink) ht_triangle(ht_scene,SX(ax,ay),SY(ax,ay),SX(bx,by),SY(bx,by),SX(cx,cy),SY(cx,cy),ink)
 ST(-15,-20,12,-22,18,17-young,skin);ST(-15,-20,18,17-young,-4,30-young,skin);
 ST(-15,-20,-4,30-young,-19,9,skin);ST(12,-5,25,5,14,9,skin);
 SL(-3,22-young,-4,40,7,8,skin);
 SD(-9,-20,17,hair);SD(-20,-7,11,hair);SL(-24,-12,-23,29,7,4,hair);SL(-23,29,-15,44,4,2,hair);SL(-9,-31,11,-21,7,4,hair);
 SL(4,-7,13,-6,0,0,edge);if(!closed)SD(11,-4,1,edge);
 SL(13,17-young,18,15-young,0,0,edge);
 if(crease>0){int ink=skin+crease*90/256;SL(1,9,8,15,1,0,ink);SL(8,15,11,16,0,0,ink);}
#undef ST
#undef SD
#undef SL
#undef SY
#undef SX
}
static void ht_sister_face(int x,int y,int young,int crease,int ghost,bool closed,bool lying){
 ht_sister_face_toward(x,y,young,crease,ghost,closed,lying,1);
}
static ht_joint ht_sister_folded_hand(void){return ht_joint_at(200,155);}
static void ht_sister_sleep(void){
 ht_sister_room(65,106);
 ht_rect(ht_scene,35,49,59,88,105);ht_rect(ht_scene,42,57,44,70,32);
 ht_limb(ht_scene,65,58,65,126,2,2,155);ht_limb(ht_scene,44,93,85,93,2,2,150);
 ht_limb(ht_scene,64,185,431,185,4,4,211);ht_limb(ht_scene,72,188,72,224,3,2,212);ht_limb(ht_scene,419,188,419,224,3,2,201);
 ht_sister_quad(93,131,245,134,258,174,79,168,27);
 ht_limb(ht_scene,93,131,244,135,1,1,105);ht_limb(ht_scene,81,167,254,173,1,1,130);
 /* The cheek meets folded knuckles; the forearm returns to its own sleeve. */
 ht_limb(ht_scene,221,136,266,152,10,15,234);
 ht_sister_face(184,132,0,0,0,true,true);
 ht_sister_quad(244,149,288,133,420,168,423,184,205);
 ht_sister_quad(244,149,423,184,206,176,202,161,205);
 ht_limb(ht_scene,284,151,252,164,10,8,235);
 ht_joint hand=ht_sister_folded_hand();
 ht_limb(ht_scene,252,164,hand.x,hand.y+3,7,5,155);
 ht_limb(ht_scene,hand.x-8,hand.y,hand.x+7,hand.y-2,5,4,155);
 for(int n=0;n<3;++n)ht_limb(ht_scene,hand.x-5+n*4,hand.y-3,hand.x-3+n*4,hand.y,0,0,213);
 ht_limb(ht_scene,289,141,368,177,0,0,154);ht_limb(ht_scene,315,145,396,178,0,0,164);
}
static unsigned ht_sister_crease(unsigned t){return 256u-(unsigned)ht_sister_ease(t,16,160);}
static ht_joint ht_sister_tea_hand(unsigned t){int pour=ht_sister_ease(t,32,96)-ht_sister_ease(t,128,180);return ht_joint_at(297-pour*9/256,155-pour*15/256);}
static void ht_sister_tea(unsigned t){
 ht_sister_room(72,115);
 ht_rect(ht_scene,50,24,95,123,23);ht_limb(ht_scene,96,25,96,145,3,3,192);ht_limb(ht_scene,52,83,143,83,2,2,170);
 ht_sister_quad(170,136,231,126,263,197,164,197,238);
 ht_limb(ht_scene,205,120,214,142,9,12,139);
 ht_sister_face(211,86,0,(int)ht_sister_crease(t),0,false,false);
 ht_limb(ht_scene,171,144,185,172,10,7,220);ht_limb(ht_scene,185,172,249,174,7,4,155);
 ht_joint hand=ht_sister_tea_hand(t);
 ht_limb(ht_scene,230,143,258,159,10,7,248);ht_limb(ht_scene,258,159,hand.x,hand.y,7,4,155);
 ht_limb(ht_scene,244,186,409,186,4,4,200);ht_limb(ht_scene,253,190,253,225,3,2,220);ht_limb(ht_scene,400,190,400,225,3,2,216);
 /* Pot follows the hand. Its stream meets the cup only while tipped. */
 int px=hand.x+9,py=hand.y+4;
 ht_disk(ht_scene,px,py,10,208);ht_limb(ht_scene,px-8,py+4,px-17,py-1,3,2,214);
 ht_limb(ht_scene,px+9,py-4,px+14,py-2,2,1,230);ht_limb(ht_scene,px+14,py-2,px+12,py+5,1,1,230);
 ht_limb(ht_scene,px-4,py-11,px+4,py-11,1,1,238);
 ht_sister_quad(267,172,285,172,283,183,270,183,218);ht_limb(ht_scene,284,175,291,176,1,1,210);ht_limb(ht_scene,291,176,286,181,1,1,210);
 if(t>=80 && t<128)ht_limb(ht_scene,px-17,py,276,174,0,0,174);
}
static void ht_sister_grass(unsigned t){
 ht_sister_room(38,80);
 ht_sister_quad(171,148,221,136,275,221,151,221,228);
 ht_sister_face(213,110,4,0,0,false,false);
 for(int n=0;n<30;++n){int x=18+n*15,y=123+(n*37)%66;ht_limb(ht_scene,x,225,x+9-(n%3)*8,y,1,0,112+(n%4)*17);ht_limb(ht_scene,x+3,196,x-9,y+24,0,0,141);}
 /* A remembered small mirror, not a fresh signal or gameplay clue. */
 ht_limb(ht_scene,210,167,244,180,8,6,243);ht_limb(ht_scene,244,180,268,156,5,3,150);
 ht_sister_quad(263,144,275,144,278,158,264,159,225);ht_rect(ht_scene,266,147,7,9,28);
 (void)t;
}
static void ht_sister_doorway(unsigned t){
 ht_sister_room(114,137);ht_rect(ht_scene,141,19,189,205,193);ht_rect(ht_scene,154,29,163,193,53);
 ht_limb(ht_scene,142,20,142,223,4,4,235);ht_limb(ht_scene,329,20,329,223,4,4,233);
 int slide=ht_sister_ease(t,0,96),x=237-slide*22/256;
 /* The younger contour recedes across the remembered tired face. */
 if(t<96)ht_sister_face(193+slide*19/256,106,4,0,105+(slide*24/256),false,false);
 ht_sister_quad(x-37,147,x+23,143,x+41,223,x-46,223,236);
 ht_limb(ht_scene,x-35,156,x-39,203,8,5,218);ht_limb(ht_scene,x+24,155,x+29,204,8,5,241);
 ht_sister_face(x,110+slide*5/256,0,0,0,false,false);
 ht_limb(ht_scene,x+2,117+slide*5/256,x+12,119+slide*5/256,0,0,187);
}
static void ht_sister_carriage(void){
 ht_sister_room(101,150);
 ht_rect(ht_scene,29,25,419,122,166);ht_rect(ht_scene,42,36,184,96,38);ht_rect(ht_scene,244,36,191,96,43);
 for(int n=0;n<3;++n){ht_limb(ht_scene,46,103+n*8,222,98+n*8,0,0,69);ht_limb(ht_scene,247,103+n*8,431,98+n*8,0,0,71);}
 ht_limb(ht_scene,235,28,235,147,4,4,218);ht_limb(ht_scene,31,146,445,146,3,3,215);
 ht_rect(ht_scene,95,169,301,35,201);ht_limb(ht_scene,96,204,395,204,4,4,230);
 ht_limb(ht_scene,110,205,110,223,3,3,234);ht_limb(ht_scene,383,205,383,223,3,3,231);
 ht_sister_quad(197,135,251,135,269,188,185,188,237);
 ht_limb(ht_scene,199,188,184,211,12,10,222);ht_limb(ht_scene,247,188,265,212,12,10,242);
 ht_limb(ht_scene,197,148,190,176,8,5,219);ht_limb(ht_scene,190,176,216,184,5,4,149);
 ht_limb(ht_scene,250,148,257,175,8,5,247);ht_limb(ht_scene,257,175,229,184,5,4,149);
 ht_sister_face(227,94,0,0,0,false,false);
}
static void ht_sister_memory_draw(const ht_night_state *s){
 unsigned t=ht_sister_memory_time(s);ht_world_scale=256;
 if(t<176)ht_sister_sleep();else if(t<368)ht_sister_tea(t-176);else if(t<464)ht_sister_grass(t-368);else if(t<592)ht_sister_doorway(t-464);else ht_sister_carriage();
 ht_checkpoint();
}
