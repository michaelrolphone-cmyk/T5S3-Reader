/* Page 25 stays on support four. The shelf and the seated body's contacts
 * share its real, flat floor; this lee-side room adds no traversal solid. */
static int ht_papers_x(void){return ht_evidence_x(8,1);}
static int ht_papers_floor(void){return ht_land_height(8,4,ht_papers_x());}
static ht_joint ht_papers_stone(void){return ht_joint_at(ht_papers_x()-2,ht_papers_floor()-24);}
static ht_joint ht_papers_burn(void){return ht_joint_at(ht_papers_x()-7,ht_papers_floor()-1);}
static void ht_papers_room(const ht_game *g){
 int x=ht_papers_x()-g->camera/256,f=ht_papers_floor()-g->camera_y/256;
 if(x+8< -ht_view_margin() || x-109>HT_W+ht_view_margin())return;
 /* Ice on the windward shoulder; soft snow and buried grass in its lee. */
 ht_triangle(ht_scene,x-110,f,x-100,f-78,x-65,f-4,195);
 ht_limb(ht_scene,x-109,f-7,x-100,f-75,2,1,75);
 ht_rect(ht_scene,x-80,f-57,83,57,173);
 ht_rect(ht_scene,x-75,f-48,23,48,91); /* Empty doorway. */
 ht_rect(ht_scene,x-48,f-51,46,48,130);
 for(int n=0;n<4;++n){int at=x-85+n*23;ht_limb(ht_scene,at,f-60,at+19,f-64+n%2*3,4,3,224);}
 ht_limb(ht_scene,x-83,f-65,x-65,f-64,1,1,50);
 ht_limb(ht_scene,x-39,f-65,x-22,f-66,1,1,51);
 ht_limb(ht_scene,x-81,f,x+1,f,1,1,201);
 for(int n=0;n<5;++n)ht_limb(ht_scene,x-48+n*10,f-3,x-42+n*10,f-1,0,0,157);
 ht_limb(ht_scene,x-18,f-21,x+2,f-21,1,1,233); /* Narrow shelf. */
 ht_limb(ht_scene,x-16,f-23,x+1,f-23,1,0,56);
 ht_limb(ht_scene,x-7,f-25,x+2,f-25,2,1,209); /* Flat stone, same contact. */
 ht_joint burn=ht_papers_burn();int bx=burn.x-g->camera/256,by=burn.y-g->camera_y/256;
 ht_limb(ht_scene,bx-4,by,bx+4,by,1,1,243);ht_limb(ht_scene,bx-2,by,bx+2,by,0,0,184);
 ht_disk(ht_scene,x-45,f-2,2,244); /* Small hole; no staged animal. */
 if(g->packet&HT_PACKET_CRUMBS)for(int n=0;n<3;++n)ht_rect(ht_scene,x-42+n*2,f-1-n%2,1,1,60);
 for(int n=0;n<4;++n){int at=x-100+n*6;ht_limb(ht_scene,at,f-2,at+3,f-7-n%2*2,0,0,153);}
 ht_checkpoint();
}
