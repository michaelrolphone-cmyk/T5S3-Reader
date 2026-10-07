/* Chapter VI: the pouch under the slab, beside the existing quarry route. */
static void ht_pouch_room(const ht_game *g){
 if(g->level!=5)return;
 int wx=ht_evidence_x(5,0),x=wx-g->camera/256,f=ht_surface_at(g,1,wx)-g->camera_y/256;
 if(x< -70-ht_view_margin() || x>HT_W+70+ht_view_margin())return;
 ht_triangle(ht_scene,x-19,f-15,x+26,f-18,x+22,f-8,131);
 ht_triangle(ht_scene,x-19,f-15,x+22,f-8,x-17,f-6,166);
 ht_limb(ht_scene,x-18,f-5,x-17,f,2,2,178);
 ht_limb(ht_scene,x+21,f-7,x+22,f,3,2,178);
 ht_limb(ht_scene,x-11,f-13,x+13,f-15,1,0,103);
 ht_disk(ht_scene,x,f-4,5,209);ht_triangle(ht_scene,x-4,f-5,x+3,f-7,x+5,f-1,213);
 ht_limb(ht_scene,x-2,f-7,x+3,f-7,1,1,231);
 ht_rect(ht_scene,x+5,f-3,5,2,77); /* Repair report under the pouch. */
 ht_checkpoint();
}
