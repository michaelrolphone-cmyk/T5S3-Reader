/* The maintenance shelter occupies the opening mountain support, before the
 * ravine. Furniture is attached to this wall; it adds no terrain or gate. */
#define HT_SHELTER_X 216
#define HT_SHELTER_BUNK_LEFT 170
#define HT_SHELTER_BUNK_RIGHT 220
#define HT_SHELTER_BASIN_X 275
#define HT_SHELTER_DOOR_X 320
static int ht_shelter_floor(void){return ht_land_height(8,0,HT_SHELTER_X);}
static int ht_shelter_bunk_top(void){return ht_shelter_floor()-10;}
static ht_joint ht_shelter_basin(void){return ht_joint_at(HT_SHELTER_BASIN_X,ht_land_height(8,0,HT_SHELTER_BASIN_X)-20);}
static void ht_shelter_room(const ht_game *g){
 if(g->level!=8)return;
 int cx=g->camera/256,cy=g->camera_y/256,x=HT_SHELTER_X-cx,f=ht_shelter_floor()-cy;
 if(x+126< -ht_view_margin() || x-84>HT_W+ht_view_margin())return;
 int left=ht_surface_at(g,0,134)-cy,right=ht_surface_at(g,0,306)-cy;
 ht_triangle(ht_scene,x-82,f-77,x+90,f-77,x+90,right,120);
 ht_triangle(ht_scene,x-82,f-77,x+90,right,x-82,left,120);
 /* Roof and wall ends have their real lower contacts on the sloping rock. */
 ht_limb(ht_scene,x-86,f-78,x+122,f-77,3,3,207);
 ht_limb(ht_scene,x-82,f-77,x-82,left,2,2,201);
 ht_limb(ht_scene,x+90,f-77,x+90,right,2,2,194);
 ht_limb(ht_scene,x+122,f-77,x+122,ht_surface_at(g,0,338)-cy,2,2,199);
 for(int n=0;n<4;++n)ht_limb(ht_scene,x-79,f-62+n*17,x+88,f-62+n*17,0,0,138);
 int by=ht_shelter_bunk_top()-cy,bl=HT_SHELTER_BUNK_LEFT-cx,br=HT_SHELTER_BUNK_RIGHT-cx;
 ht_rect(ht_scene,bl,by-2,br-bl,3,84);ht_limb(ht_scene,bl-2,by+2,br+2,by+2,2,2,209);
 /* Triangular iron brackets fasten the bunk to the wall, not phantom legs. */
 for(int n=0;n<2;++n){int wx=HT_SHELTER_BUNK_LEFT+5+n*36,bx=wx-cx,down=ht_min(9,ht_surface_at(g,0,wx)-cy-by-1);ht_limb(ht_scene,bx,by+3,bx,by+down,1,1,188);ht_limb(ht_scene,bx,by+down,bx+8,by+3,1,1,190);ht_disk(ht_scene,bx,by+down-1,1,225);}
 ht_rect(ht_scene,x+18,f-52,57,3,175);
 for(int n=0;n<3;++n){int hx=x-59+n*20;ht_limb(ht_scene,hx,f-64,hx,f-59,1,1,200);ht_limb(ht_scene,hx,f-59,hx+3,f-61,1,0,201);}
 ht_joint basin=ht_shelter_basin();int bx=basin.x-cx,byy=basin.y-cy;
 ht_triangle(ht_scene,bx-12,byy,bx+12,byy,bx+8,byy+8,192);
 ht_triangle(ht_scene,bx-12,byy,bx+8,byy+8,bx-8,byy+8,174);
 ht_limb(ht_scene,bx-12,byy,bx+12,byy,1,1,217);ht_limb(ht_scene,bx-9,byy-1,bx+9,byy-1,0,0,93);
 ht_limb(ht_scene,bx-9,byy+7,bx-9,byy+12,1,1,202);ht_limb(ht_scene,bx-9,byy+12,bx+7,byy+7,1,1,202);
 ht_checkpoint();
}
