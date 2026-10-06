/* Optional ferry cover/oar and reed detail; the approved hull is untouched. */
static void ht_ferry_boat_draw(const ht_game *g) {
 if(g->level!=4 || (!g->traversal.ferry_line_taken && !g->traversal.ferry_retrieved && g->traversal.boat_x!=ht_mech(g)->boat_right*256))return;
 int cx=g->camera/256,cy=g->camera_y/256;
 int boat=g->traversal.boat_x/256,x=boat-cx,y=ht_boat_top(g)/256-cy;
 int distance=ht_clamp((boat-ht_mech(g)->boat_left)*256/(ht_mech(g)->boat_right-ht_mech(g)->boat_left),0,256);
 int fold=6+distance*14/256;
 /* One bent canvas cover; no head, face, living occupant or second hull. */
 ht_triangle(ht_scene,x+16,y-2,x+26,y-fold,x+37,y-2,217);
 ht_limb(ht_scene,x+18,y-4,x+25,y-fold+1,2,1,244);
 ht_limb(ht_scene,x+25,y-fold+1,x+33,y-9,1,2,230);
 ht_limb(ht_scene,x+25,y-fold+4,x+32,y-3,0,0,107);
 /* The same pale strip resolves into a long oar as the cover settles. */
 ht_limb(ht_scene,x+17,y-9,x+41,y-5,1,1,100);
 ht_limb(ht_scene,x+17,y-9,x+20,y-9,2,1,75);
 if(distance<192){
  ht_limb(ht_scene,x-25,y+5,x-11,y+6,0,0,122);
  ht_limb(ht_scene,x-7,y+7,x+7,y+7,0,0,142);
  ht_limb(ht_scene,x-20,y+11,x-8,y+12,1,1,170);
  ht_rect(ht_scene,x+1,y+9,4,3,80);ht_limb(ht_scene,x+4,y+9,x+6,y+10,0,0,115);
 }
 int water=ht_mech(g)->boat_deck+10-cy;
 /* Reeds cross the cover and its low reflected fragments, never the grip. */
 for(int i=0;i<9;++i){int rx=ht_mech(g)->water_right-78+i*9-cx,base=water+10+i%3*3;
  ht_limb(ht_scene,rx,base,rx-8+i%3,base-37-i%4*8,0,0,i%2?160:231);}
 for(int i=0;i<3;++i)ht_limb(ht_scene,x+16+i*3,water+8+i*5,x+33-i*2,water+8+i*5,0,0,138-i*12);
}
