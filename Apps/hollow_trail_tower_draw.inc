/* Existing tower supports only: first landing before the channel, final stair
 * after it. Scene furniture adds no collision, route or evidence gate. */
#define HT_TOWER_BENCH_X 1140
static int ht_tower_floor(void){return ht_land_height(9,3,HT_TOWER_BENCH_X);}
static ht_joint ht_tower_hook(unsigned n){return ht_joint_at(HT_TOWER_BENCH_X-37+(int)n*10,ht_tower_floor()-(n==7?25:36));}
static ht_joint ht_tower_chip(void){ht_joint h=ht_tower_hook(7);return ht_joint_at(h.x-5,h.y+2);}
static ht_joint ht_tower_card_contact(void){return ht_joint_at(ht_evidence_x(9,1),ht_land_height(9,8,ht_evidence_x(9,1))-24);}
static void ht_tower_hooks(const ht_game *g){
 int cx=g->camera/256,cy=g->camera_y/256,x=HT_TOWER_BENCH_X-cx,f=ht_tower_floor()-cy;
 if(x+88< -ht_view_margin() || x-92>HT_W+ht_view_margin())return;
 ht_rect(ht_scene,x-87,f-82,175,82,112);
 /* Damp plaster never becomes a legible map; the sheltered square is blank. */
 for(int n=0;n<16;++n){int dx=x-79+(n*23)%157,dy=f-76+(n*17)%49;ht_disk(ht_scene,dx,dy,3+n%5,125+n%3*6);}
 ht_rect(ht_scene,x-10,f-38,22,23,66);
 ht_rect(ht_scene,x-44,f-40,80,6,176);
 for(unsigned n=0;n<8;++n){ht_joint h=ht_tower_hook(n);int hx=h.x-cx,hy=h.y-cy;
  if(n==7)for(int k=0;k<5;++k)ht_rect(ht_scene,hx-4+k*2,hy-4+(k%2)*3,2,2,45);
  ht_limb(ht_scene,hx,hy-3,hx,hy+3,1,1,230);ht_limb(ht_scene,hx,hy+3,hx+3,hy+2,1,0,230);
 }
 ht_joint chip=ht_tower_chip();ht_rect(ht_scene,chip.x-cx-1,chip.y-cy-1,2,2,45);
 ht_limb(ht_scene,x-44,f-11,x+43,f-11,2,2,213);
 for(int side=-1;side<=1;side+=2)ht_limb(ht_scene,x+side*37,f-10,x+side*37,ht_surface_at(g,3,HT_TOWER_BENCH_X+side*37)-cy,1,1,225);
 /* Clipped cable follows the actual stair wall; worn hollows are surface marks. */
 ht_limb(ht_scene,x-80,f-78,x-80,f-9,1,1,190);
 for(int n=0;n<4;++n)ht_rect(ht_scene,x-83,f-70+n*16,7,2,215);
 for(int n=0;n<3;++n)ht_limb(ht_scene,x-72+n*12,f-3,x-65+n*12,f-1,1,0,143);
 ht_checkpoint();
}
static void ht_tower_card_draw(const ht_game *g){
 ht_joint c=ht_tower_card_contact();int x=c.x-g->camera/256,y=c.y-g->camera_y/256;
 if(x+44< -ht_view_margin() || x-70>HT_W+ht_view_margin())return;
 /* Thick older masonry surrounds a shoulder-width light strip. */
 ht_rect(ht_scene,x-67,y-53,110,77,151);ht_rect(ht_scene,x-57,y-46,20,48,207);ht_rect(ht_scene,x-51,y-42,8,39,54);
 ht_triangle(ht_scene,x-51,y-3,x-43,y-3,x-20,y+24,79);
 ht_rect(ht_scene,x-17,y-5,34,5,174);
 ht_rect(ht_scene,x-10,y+3,24,18,83); /* service record underneath */
 ht_rect(ht_scene,x-9,y+1,19,13,40);
 for(int n=0;n<3;++n)ht_rect(ht_scene,x-6,y+5+n*3,12,1,168);
 ht_limb(ht_scene,x-2,y-3,x-2,y+3,1,1,199);ht_limb(ht_scene,x-2,y+3,x+3,y+3,1,1,215); /* brass clip */
 ht_rect(ht_scene,x+3,y+17,8,1,208);ht_disk(ht_scene,x+12,y+17,1,229);
 ht_checkpoint();
}
static void ht_tower_room(const ht_game *g){if(g->level!=9)return;ht_tower_hooks(g);ht_tower_card_draw(g);}
