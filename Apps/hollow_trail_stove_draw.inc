/* Chapter III: the cold shed after the culvert, on existing terrace five.
 * Furniture is behind the walking plane; no new collision or warmth source. */
static void ht_stove_coat_marks(int x,int f) {
 for(int n=0;n<3;++n) {
  int a=x-65+n*19,top=f-61+(n%2)*3;
  /* Broken pale edges, no sleeves or coat body hanging in the room. */
  ht_limb(ht_scene,a-2,top,a-8,top+13,1,1,98);
  ht_limb(ht_scene,a+2,top,a+7,top+10,1,1,104);
  ht_limb(ht_scene,a-5,top+16,a-5,top+29-n,1,1,106);
  ht_limb(ht_scene,a+5,top+19,a+5,top+30-n,1,1,106);
  ht_limb(ht_scene,a-4,top+30-n,a+4,top+30-n,1,1,102);
 }
}
static void ht_stove_room(const ht_game *g,unsigned pulse,unsigned age) {
 if(g->level!=2)return;
 int cx=g->camera/256,cy=g->camera_y/256,x=HT_STOVE_X-cx,f=ht_stove_floor()-cy;
 if(x+105< -ht_view_margin() || x-95>HT_W+ht_view_margin())return;
 ht_rect(ht_scene,x-92,f-84,192,84,146);
 /* Quiet warped boards and soot accumulate around the cold pipe. */
 for(int n=0;n<10;++n) {
  int a=x-81+n*17;
  ht_limb(ht_scene,a,f-79,a+(n%3)-1,f-7,0,0,153);
  if(n%3==1)ht_limb(ht_scene,a+5,f-70,a+6,f-51,0,0,138);
 }
 ht_triangle(ht_scene,x-92,f-84,x-79,f-74,x-79,f,177);
 ht_triangle(ht_scene,x-92,f-84,x-79,f,x-92,f,177);
 ht_rect(ht_scene,x-79,f,172,5,184);
 for(int n=0;n<7;++n)ht_limb(ht_scene,x-76+n*25,f,x-73+n*25,f+4,0,0,133);
 for(int n=0;n<5;++n)ht_limb(ht_scene,x-4+n*6,f-73,x-7+n*7,f-41+(n%2)*7,2,1,158);
 ht_triangle(ht_scene,x-95,f-84,x+8,f-101,x+105,f-84,211);
 ht_limb(ht_scene,x-95,f-84,x+105,f-84,2,2,230);
 ht_rect(ht_scene,x-92,f-82,7,82,212);
 ht_rect(ht_scene,x+94,f-82,6,82,212);
 /* Doorway at the right admits cold daylight; no fire or chimney smoke. */
 ht_rect(ht_scene,x+68,f-64,23,64,65);
 ht_rect(ht_scene,x+64,f-67,4,67,222);
 ht_stove_coat_marks(x,f);
 /* Worn bench and two fresh drag marks beyond the old feet. */
 ht_limb(ht_scene,x-75,f-11,x-26,f-11,2,2,205);
 ht_limb(ht_scene,x-70,f-9,x-71,f,1,1,225);
 ht_limb(ht_scene,x-31,f-9,x-32,f,1,1,225);
 ht_limb(ht_scene,x-69,f,x-62,f,0,0,79);
 ht_limb(ht_scene,x-30,f,x-22,f,0,0,79);
 /* Body and feet of the cold iron stove. */
 ht_rect(ht_scene,x+10,f-29,33,24,220);
 ht_rect(ht_scene,x+38,f-28,5,23,240);
 ht_limb(ht_scene,x+8,f-30,x+45,f-30,2,2,235);
 ht_rect(ht_scene,x+16,f-24,20,15,185);
 ht_rect(ht_scene,x+19,f-21,12,8,240);
 ht_disk(ht_scene,x+35,f-17,1,85);
 ht_limb(ht_scene,x+19,f-10,x+32,f-10,0,0,112);
 ht_limb(ht_scene,x+12,f-26,x+12,f-13,0,0,176);
 for(int side=0;side<2;++side)ht_limb(ht_scene,x+14+side*25,f-6,x+12+side*29,f,1,1,235);
 /* Sorted fuel beneath, thin sticks crossed on the thicker pieces. */
 for(int n=0;n<4;++n)ht_limb(ht_scene,x+17,f-3-n*2,x+33-n*2,f-3-n*2,1,1,177);
 ht_limb(ht_scene,x+18,f-10,x+33,f-7,0,0,109);
 ht_limb(ht_scene,x+20,f-7,x+31,f-11,0,0,109);
 /* Pipe returns into the wall. The existing wire is wrapped in soot-stiff cloth. */
 int shake=pulse==3 && age<16?((age&2)?1:-1):0;
 ht_limb(ht_scene,x+14,f-31,x+14,f-58,3,3,214);
 ht_limb(ht_scene,x+14,f-58,x+1,f-58,3,3,218);
 ht_limb(ht_scene,x+1,f-58,x+1+shake,f-25,3,3,219);
 ht_rect(ht_scene,x-3+shake,f-35,8,9,166);
 for(int k=0;k<4;++k)ht_limb(ht_scene,x-3+shake,f-34+k*2,x+5+shake,f-32+k*2,0,0,223);
 /* Loose plate and folded note, not a free-floating evidence icon. */
 ht_rect(ht_scene,x+46,f-19,10,13,179);
 ht_triangle(ht_scene,x+47,f-18,x+56,f-17,x+54,f-7,112);
 ht_limb(ht_scene,x+48,f-15,x+52,f-14,0,0,204);
 ht_limb(ht_scene,x+48,f-12,x+51,f-11,0,0,204);
 /* Shallow rainwater dish. Two knocks make rings; the third rocks all water. */
 ht_limb(ht_scene,x+26,f-34,x+41,f-34,1,1,235);
 ht_limb(ht_scene,x+27,f-33,x+30,f-30,1,1,220);
 ht_limb(ht_scene,x+40,f-33,x+37,f-30,1,1,220);
 ht_rect(ht_scene,x+29,f-33,10,2,65);
 if(pulse && age<24) {
  if(pulse==3)for(int k=0;k<5;++k)ht_rect(ht_scene,x+29+k*2,f-33+((k+age)&1),1,1,226);
  else {
   int radius=1+(int)age/5;
   ht_limb(ht_scene,x+34-radius,f-33,x+34-radius+1,f-32,0,0,190);
   ht_limb(ht_scene,x+34+radius-1,f-32,x+34+radius,f-33,0,0,190);
  }
 } else ht_limb(ht_scene,x+35,f-33,x+37,f-32,0,0,177); /* doorway reflected again */
 if(pulse==3 && age<24)for(int k=0;k<6;++k)
  ht_rect(ht_scene,x-2+k%4,f-25+(int)age/2+k*2,1,1,213);
 ht_checkpoint();
}
