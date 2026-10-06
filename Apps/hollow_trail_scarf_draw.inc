/* The original page-24 post stays on support one. Its cap, tracing, wire and
 * cloth use the same contacts as the examination. No new collision surface. */
static int ht_scarf_x(void){return ht_evidence_x(8,0);}
static int ht_scarf_floor(void){return ht_land_height(8,1,ht_scarf_x());}
static ht_joint ht_scarf_knot(void){return ht_joint_at(ht_scarf_x()+2,ht_scarf_floor()-25);}
static void ht_scarf_post(const ht_game *g){
 int x=ht_scarf_x()-g->camera/256,f=ht_scarf_floor()-g->camera_y/256;
 if(x+46< -ht_view_margin() || x-25>HT_W+ht_view_margin())return;
 ht_limb(ht_scene,x-10,f,x-12,f-36,3,2,225);
 for(int n=0;n<3;++n)ht_limb(ht_scene,x-13+n*2,f-32,x-11+n*2,f-3,0,0,173+n*9);
 ht_limb(ht_scene,x-12,f-33,x+41,ht_surface_at(g,1,ht_scarf_x()+41)-g->camera_y/256,0,0,154);
 ht_rect(ht_scene,x-10,f-36,29,21,73); /* tracing secured below the cap */
 ht_limb(ht_scene,x-3,f-20,x-3,f-32,0,0,182);ht_limb(ht_scene,x-3,f-32,x+8,f-32,0,0,182);
 ht_limb(ht_scene,x+8,f-32,x+8,f-20,0,0,182);
 for(int n=0;n<3;++n)ht_limb(ht_scene,x-3,f-22-n*3,x+8,f-22-n*3,0,0,157);
 ht_limb(ht_scene,x-13,f-37,x+20,f-37,2,1,241);
 if(!g->scarf){
  ht_joint k=ht_scarf_knot();int sx=k.x-g->camera/256,sy=k.y-g->camera_y/256;
  ht_limb(ht_scene,x-12,f-25,sx,sy,1,1,143);
  ht_triangle(ht_scene,sx-3,sy,sx+4,sy+1,sx+3,sy+16,144);
  ht_triangle(ht_scene,sx-3,sy,sx+3,sy+16,sx-2,sy+13,157);
  for(int n=0;n<4;++n)ht_limb(ht_scene,sx-2+n*2,sy+12,sx-3+n*2,sy+17-n%2,0,0,135);
  ht_limb(ht_scene,sx,sy+4,sx+2,sy+3,0,0,215);
 }
 ht_checkpoint();
}
