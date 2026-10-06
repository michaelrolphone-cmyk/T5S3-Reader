/* The far valve occupies page 22's existing support-four landing. Furniture
 * shares its contact coordinates with the examination, never a new foothold. */
static int ht_valve_x(void){return ht_evidence_x(7,1);}
static int ht_valve_floor(void){return ht_land_height(7,4,ht_valve_x());}
static ht_joint ht_valve_contact(void){return ht_joint_at(ht_valve_x(),ht_valve_floor()-23);}
static ht_joint ht_valve_note_contact(void){return ht_joint_at(ht_valve_x()+12,ht_valve_floor()-26);}
static void ht_valve_room(const ht_game *g){
 if(g->level!=7)return;
 int x=ht_valve_x()-g->camera/256,f=ht_valve_floor()-g->camera_y/256;
 if(x+97< -ht_view_margin() || x-118>HT_W+ht_view_margin())return;
 /* A masonry overhang shelters the notebook; the wide return enters the wall. */
 ht_triangle(ht_scene,x-117,f-69,x+88,f-73,x+83,f-61,175);
 ht_triangle(ht_scene,x-117,f-69,x+83,f-61,x-104,f-56,166);
 ht_rect(ht_scene,x-109,f-47,169,18,151);
 ht_limb(ht_scene,x-109,f-45,x+59,f-45,1,1,101);
 ht_limb(ht_scene,x-109,f-30,x+59,f-30,1,1,210);
 ht_rect(ht_scene,x-54,f-44,37,11,84);
 for(int n=0;n<14;++n)ht_disk(ht_scene,x-52+(n*7)%34,f-43+(n*3)%9,2,105+n%3*10);
 ht_rect(ht_scene,x-9,f-34,19,32,180);ht_rect(ht_scene,x-16,f-3,34,3,218);
 ht_joint c=ht_valve_contact();int wy=c.y-g->camera_y/256;
 ht_disk(ht_scene,x,wy,13,215);ht_disk(ht_scene,x,wy,10,125);
 ht_limb(ht_scene,x-10,wy,x+10,wy,1,1,225);ht_limb(ht_scene,x,wy-10,x,wy+10,1,1,225);
 ht_disk(ht_scene,x,wy,3,229);
 ht_limb(ht_scene,x+6,wy-8,x+12,wy-3,0,0,232);
 ht_joint note=ht_valve_note_contact();int nx=note.x-g->camera/256,ny=note.y-g->camera_y/256;
 ht_rect(ht_scene,nx-1,ny,10,13,44);ht_rect(ht_scene,nx+1,ny+3,6,1,155);ht_rect(ht_scene,nx+1,ny+6,5,1,155);
 /* Maintenance box and notebook have different silhouettes. */
 ht_rect(ht_scene,x+35,f-22,41,22,205);ht_rect(ht_scene,x+38,f-19,35,16,122);
 ht_rect(ht_scene,x+50,f-24,12,3,219);ht_rect(ht_scene,x+53,f-15,7,3,217);
 ht_rect(ht_scene,x-100,f-14,65,5,184);
 ht_rect(ht_scene,x-90,f-21,42,7,144);ht_rect(ht_scene,x-88,f-20,18,5,65);ht_rect(ht_scene,x-68,f-20,18,5,61);
 ht_limb(ht_scene,x-67,f-19,x-53,f-18,1,1,226);ht_limb(ht_scene,x-53,f-18,x-46,f-18,1,1,154);
 ht_checkpoint();
}
