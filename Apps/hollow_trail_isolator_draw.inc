/* The existing WIRE control houses the hinged inward contact. */
#define HT_ISOLATOR_X (HT_PUZZLE_FIRST+3*HT_PUZZLE_SPACING)
#define HT_ISOLATOR_WAIT 576
static unsigned ht_isolator_lift(unsigned tick){return tick<192?0:ht_min(4,(tick-192)*4/48);}
static unsigned ht_isolator_lid(unsigned stage,unsigned tick){
 if(stage==2)return 0;
 if(stage==1)return tick<64?64:128-ht_min(128,tick);
 return tick<64?0:ht_min(64,tick-64);
}
static void ht_isolator_box(const ht_game *g,unsigned lid,unsigned lift){
 if(g->level!=8)return;
 int x=HT_ISOLATOR_X-g->camera/256,f=ht_level_land(8)[9].top-g->camera_y/256;
 if(x< -45-ht_view_margin() || x>HT_W+45+ht_view_margin())return;
 ht_rect(ht_scene,x-14,f-47,29,38,219);ht_rect(ht_scene,x-12,f-45,25,34,107);
 /* Fixed contact and hinge; the silver underside separates from its mate. */
 ht_rect(ht_scene,x+2,f-28,7,3,235);ht_disk(ht_scene,x-5,f-29,2,217);
 ht_limb(ht_scene,x-5,f-29,x+3,f-29-(int)lift,1,1,234);
 if(lift)ht_limb(ht_scene,x-4,f-27,x+3,f-27-(int)lift,1,1,30);
 ht_limb(ht_scene,x-6,f-44,x-5,f-31,1,1,190);ht_limb(ht_scene,x+6,f-25,x+6,f-11,1,1,191);
 /* Its lid swings clear; closure never changes the electrical contact. */
 int edge=x+13-(int)lid*37/64;
 ht_triangle(ht_scene,x-14,f-47,edge,f-44,edge,f-12,201);
 ht_triangle(ht_scene,x-14,f-47,edge,f-12,x-14,f-9,213);
 ht_disk(ht_scene,edge-2,f-29,1,70);
 if(lid<12)for(int n=0;n<5;++n)ht_rect(ht_scene,x-10+n*4,f-46,2,2,66+n*7);
 ht_checkpoint();
}
