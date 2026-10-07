/* The separate wheel-less sleeper precedes the intact train on the far track. */
#define HT_CARRIAGE_X 140
#define HT_CARRIAGE_SHIFT (HT_CARRIAGE_X-260)
static int ht_carriage_ground(void){return ht_land_height(3,0,HT_CARRIAGE_X-10);}
static void ht_carriage_shell(const ht_game *g,bool inside,bool boots){
 if(g->level!=3)return;
 int cx=g->camera/256,cy=g->camera_y/256,x=HT_CARRIAGE_X-cx,f=ht_carriage_ground()-cy;
 if(x+220< -ht_view_margin() || x>HT_W+ht_view_margin())return;
 /* Underframe rests on timber, with no wheels or invented ladder. */
 ht_rect(ht_scene,x-6,f-8,19,8,213);ht_rect(ht_scene,x+166,f-8,21,8,213);
 for(int k=0;k<3;++k){ht_rect(ht_scene,x-4,f-7+k*2,15,1,139);ht_rect(ht_scene,x+168,f-7+k*2,17,1,139);}
 ht_rect(ht_scene,x-5,f-15,218,6,230);
 ht_rect(ht_scene,x,f-84,210,69,inside?72:166);
 ht_limb(ht_scene,x-6,f-84,x+12,f-94,3,3,223);ht_limb(ht_scene,x+12,f-94,x+195,f-94,3,3,223);ht_limb(ht_scene,x+195,f-94,x+216,f-84,3,3,223);
 /* Swollen side door remains shut; the broken end window is the entry. */
 ht_rect(ht_scene,x+157,f-79,35,63,200);ht_rect(ht_scene,x+161,f-75,27,54,148);ht_rect(ht_scene,x+165,f-44,4,3,245);
 if(!inside){
  for(int n=0;n<3;++n){int wx=x+37+n*37;ht_rect(ht_scene,wx,f-76,27,31,59);ht_rect(ht_scene,wx+12,f-76,2,31,194);ht_limb(ht_scene,wx+2,f-72,wx+21,f-54,1,1,104);}
  for(int n=0;n<8;++n)ht_rect(ht_scene,x+23+n*16,f-33,12,1,120);
 }else{
  /* Exposed ceiling ribs, luggage rack and the strap's two split tongues. */
  for(int n=0;n<6;++n){int rx=x+25+n*25;ht_limb(ht_scene,rx,f-89,rx+9,f-80,1,1,173);ht_limb(ht_scene,rx+9,f-80,rx+13,f-69,1,1,173);}
  ht_rect(ht_scene,x+23,f-68,130,3,190);
  ht_limb(ht_scene,x+107,f-68,x+105,f-48,1,1,219);ht_limb(ht_scene,x+105,f-48,x+101,f-38,1,1,219);ht_limb(ht_scene,x+105,f-48,x+109,f-40,1,1,219);
  /* Dry bench and rain-stained opposite seat; the aisle stays legible. */
  ht_rect(ht_scene,x+40,f-24,40,4,193);ht_rect(ht_scene,x+41,f-44,38,18,139);
  ht_rect(ht_scene,x+43,f-20,3,8,208);ht_rect(ht_scene,x+74,f-20,3,8,208);
  ht_rect(ht_scene,x+105,f-24,37,4,193);ht_rect(ht_scene,x+107,f-45,34,20,139);
  for(int n=0;n<5;++n)ht_rect(ht_scene,x+110+n*5,f-43+(n%2)*3,3,10+n%3,191);
  ht_rect(ht_scene,x+108,f-75,32,25,42);ht_rect(ht_scene,x+122,f-75,2,25,183);
  ht_limb(ht_scene,x+111,f-72,x+135,f-55,1,1,82);
  /* The warning retains the book's underlined word. */
  ht_rect(ht_scene,x+28,f-61,66,22,31);ht_text_color(x+31,f-59,"PASSENGERS",1,181);
  ht_rect(ht_scene,x+31,f-51,59,1,195);ht_text_color(x+31,f-49,"NO LEANING",1,181);
  if(boots){
   ht_rect(ht_scene,x+45,f-18,4,6,226);ht_rect(ht_scene,x+41,f-14,8,3,226);
   ht_rect(ht_scene,x+56,f-18,4,6,226);ht_rect(ht_scene,x+52,f-14,8,3,226);
  }
 }
 /* Broken opening retains its jambs, sill and a few attached glass corners. */
 ht_rect(ht_scene,x,f-68,16,42,48);
 ht_rect(ht_scene,x-2,f-70,3,47,225);ht_rect(ht_scene,x+15,f-70,3,47,225);
 ht_rect(ht_scene,x-2,f-28,20,3,225);
 ht_triangle(ht_scene,x+1,f-67,x+6,f-67,x+1,f-59,129);
 ht_triangle(ht_scene,x+14,f-65,x+14,f-58,x+10,f-65,129);
 ht_checkpoint();
}
static void ht_carriage_window_front(const ht_game *g){
 int x=HT_CARRIAGE_X-g->camera/256,f=ht_carriage_ground()-g->camera_y/256;
 ht_rect(ht_scene,x,f-25,16,10,174);ht_rect(ht_scene,x-2,f-28,20,3,225);
 ht_rect(ht_scene,x-2,f-70,3,43,225);ht_rect(ht_scene,x+15,f-70,3,43,225);
}
