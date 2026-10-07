/* The first house precedes the settlement's lamp terrace. */
#define HT_FIRST_HOUSE_X 270
static int ht_first_house_floor(void){return ht_land_height(9,0,HT_FIRST_HOUSE_X);}
static void ht_first_house_room(const ht_game *g,int lean){
 if(g->level!=9 || g->door_stage)return;
 int x=HT_FIRST_HOUSE_X-g->camera/256,f=ht_first_house_floor()-g->camera_y/256;
 if(x+85< -ht_view_margin() || x-70>HT_W+ht_view_margin())return;
 ht_rect(ht_scene,x-55,f-60,126,60,158);
 ht_triangle(ht_scene,x-63,f-59,x+12,f-75,x+80,f-57,212);
 ht_limb(ht_scene,x-63,f-59,x+12,f-75,2,2,221);ht_limb(ht_scene,x+12,f-75,x+80,f-57,2,2,221);
 /* Rough stone and whitewash surviving in joints and under the eaves. */
 for(int row=0;row<5;++row)for(int col=0;col<7;++col){
  int xx=x-53+col*18+(row%2)*4,yy=f-56+row*11;
  ht_limb(ht_scene,xx,yy,xx+13,yy-1,1,0,row<2?96:133);
  ht_rect(ht_scene,xx+14,yy+1,1,7,125);
 }
 ht_rect(ht_scene,x+31,f-46,26,46,214);ht_rect(ht_scene,x+34,f-43,20,43,184);
 ht_limb(ht_scene,x+35,f-42,x+35,f-2,1,1,230);ht_disk(ht_scene,x+51,f-23,1,71);
 /* Window, divided pane, interior condensation and its narrow clear channel. */
 ht_rect(ht_scene,x-12,f-51,29,33,220);ht_rect(ht_scene,x-9,f-48,23,27,85);
 ht_rect(ht_scene,x-9,f-35,23,14,120);
 for(int n=0;n<28;++n){int xx=x-8+(int)(ht_hash(n+130)%21),yy=f-34+(int)(ht_hash(n+418)%12);ht_rect(ht_scene,xx,yy,1,2,138+n%3*9);}
 ht_rect(ht_scene,x-4,f-34,10,12,79);
 int reflected=x-1+lean/5;if(reflected>=x-4 && reflected<=x+4)ht_rect(ht_scene,reflected,f-34,2,12,185);
 ht_rect(ht_scene,x-10,f-37,25,2,216);
 ht_rect(ht_scene,x-15,f-20,35,3,193);ht_rect(ht_scene,x-10,f-19,22,2,60);
 /* Dead moth and crack in the paint, lit by the pale rectangle on the sill. */
 ht_disk(ht_scene,x+1,f-20,1,226);ht_triangle(ht_scene,x,f-20,x-3,f-22,x-2,f-19,190);ht_triangle(ht_scene,x+1,f-20,x+4,f-22,x+3,f-19,190);
 ht_limb(ht_scene,x+8,f-19,x+10,f-18,1,0,219);ht_limb(ht_scene,x+10,f-18,x+12,f-20,1,0,219);
 /* The opposing doorway's edge is behind the observer, reflected in glass. */
 int post=HT_FIRST_HOUSE_X-65,ground=ht_surface_at(g,0,post)-g->camera_y/256;
 ht_limb(ht_scene,post-g->camera/256,ground,post-g->camera/256,ground-49,2,2,224);
 ht_limb(ht_scene,post-g->camera/256,ground-49,post-g->camera/256-18,ground-49,2,2,214);
 ht_checkpoint();
}
