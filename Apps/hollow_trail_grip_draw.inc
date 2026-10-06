/* The original brake-notice site, now anchored to a cable casing and grip.
 * This is scenery beside support four; the working balance stays at the exit. */
static int ht_grip_x(void){return ht_evidence_x(5,1);}
static int ht_grip_floor(void){return ht_land_height(5,4,ht_grip_x());}
static ht_joint ht_grip_contact(void){return ht_joint_at(ht_grip_x(),ht_grip_floor()-21);}
static void ht_grip_room(const ht_game *g){
 if(g->level!=5)return;
 int x=ht_grip_x()-g->camera/256,f=ht_grip_floor()-g->camera_y/256;
 if(x<-90-ht_view_margin() || x>HT_W+90+ht_view_margin())return;
 ht_rect(ht_scene,x+7,f-36,28,36,164);ht_rect(ht_scene,x+10,f-33,22,29,126);
 ht_disk(ht_scene,x+19,f-17,9,213);ht_disk(ht_scene,x+19,f-17,6,105);
 ht_limb(ht_scene,x+20,f-17,x-4,f-24,2,2,206);
 ht_limb(ht_scene,x-4,f-24,x+5,f-21,2,2,153);
 ht_limb(ht_scene,x-3,f-25,x+5,f-22,0,0,230);
 ht_limb(ht_scene,x+5,f-22,x+3,f-16,0,0,198);
 for(int n=0;n<3;++n)ht_limb(ht_scene,x-3+n*3,f-25,x-4+n*3,f-22,0,0,93);
 ht_rect(ht_scene,x+13,f-32,17,9,55);ht_rect(ht_scene,x+15,f-30,11,1,161);
 /* The cable remains still; touching the repaired wood never moves a load. */
 ht_limb(ht_scene,x+27,f-24,x+48,f-53,0,0,185);ht_limb(ht_scene,x+48,f-53,x+72,f-60,0,0,166);
 ht_disk(ht_scene,x+48,f-53,3,220);ht_disk(ht_scene,x+48,f-53,1,91);
}
