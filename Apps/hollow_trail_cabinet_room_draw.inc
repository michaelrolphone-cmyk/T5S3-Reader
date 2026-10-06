/* Chapter X's modest room. Choice/read state remains owned by the existing UI. */
static void ht_cabinet_room(const ht_game *g){
 if(g->level!=9 || g->door_stage || !g->puzzle.solved)return;
 int x=HT_GOAL-g->camera/256,f=ht_level_land(9)[9].top-g->camera_y/256;
 if(x+68< -ht_view_margin() || x-65>HT_W+ht_view_margin())return;
 /* One broad, recessed opening retains the actual settlement backdrop. */
 ht_rect(ht_scene,x-62,f-73,76,73,173);ht_rect(ht_scene,x+54,f-73,10,73,173);
 ht_rect(ht_scene,x+14,f-73,40,5,173);ht_rect(ht_scene,x+14,f-41,40,41,173);
 for(int row=0;row<7;++row)for(int col=0;col<7;++col){
  int xx=x-59+col*17+(row%2)*3,yy=f-68+row*10;
  if(xx+13>=x+13 && xx<x+55 && yy<f-39)continue;
  ht_limb(ht_scene,xx,yy,ht_min(x+61,xx+13),yy-1,1,0,146);ht_rect(ht_scene,xx,yy,1,7,155);
 }
 ht_rect(ht_scene,x-64,f-75,130,3,210);ht_rect(ht_scene,x-64,f-72,3,72,202);ht_rect(ht_scene,x+63,f-72,3,72,205);
 ht_rect(ht_scene,x+12,f-70,3,31,220);ht_rect(ht_scene,x+53,f-70,3,31,213);
 ht_rect(ht_scene,x+12,f-70,44,2,215);ht_rect(ht_scene,x+10,f-41,48,3,210);
 ht_limb(ht_scene,x+38,f-37,x+38,f-8,1,1,221);ht_limb(ht_scene,x+38,f-8,x,f-8,1,1,209);
 /* Rubbing pinned to the wall, without fabricated wording or date numerals. */
 ht_triangle(ht_scene,x-53,f-57,x-35,f-58,x-33,f-39,95);ht_triangle(ht_scene,x-53,f-57,x-33,f-39,x-52,f-38,103);
 ht_disk(ht_scene,x-45,f-56,1,229);
 for(int n=0;n<4;++n)ht_limb(ht_scene,x-49,f-51+n*3,x-37,f-52+n*3,1,0,170);
 /* The heavy table stands on the actual floor and is bolted down. */
 ht_rect(ht_scene,x-34,f-17,68,4,204);ht_rect(ht_scene,x-34,f-18,68,1,116);
 for(int side=-1;side<=1;side+=2){int foot=x+side*25;ht_limb(ht_scene,foot,f-14,foot+side,f,2,2,220);ht_rect(ht_scene,foot-4,f-1,9,2,193);ht_disk(ht_scene,foot+side*3,f-1,1,66);}
 /* Two worn hollows on the mat before the controls; no chairs. */
 ht_triangle(ht_scene,x-57,f,x-22,f,x-19,f+4,146);ht_triangle(ht_scene,x-57,f,x-19,f+4,x-59,f+3,156);
 for(int n=0;n<2;++n){int at=x-32+n*7;ht_limb(ht_scene,at,f+1,at+2,f+2,1,1,191);}
 ht_checkpoint();
}
static void ht_cabinet_case(const ht_game *g,int f){
 int x=HT_GOAL-g->camera/256,b=f-18,top=b-34;
 /* Both grips share one slotted plate, entering opposite sides of the case. */
 ht_rect(ht_scene,x-41,b-5,23,5,183);ht_rect(ht_scene,x-39,b-3,19,1,91);
 int black=x-35,white=x-24,black_y=b-9+(g->verdict==1?8:0),white_y=b-9+(g->verdict==2?8:0);
 ht_limb(ht_scene,black,b-1,x-18,b-10,1,1,154);
 ht_limb(ht_scene,white,b-1,white,b+5,1,1,162);ht_limb(ht_scene,white,b+5,x+20,b+5,1,1,162);ht_limb(ht_scene,x+20,b+5,x+20,b-10,1,1,162);
 ht_limb(ht_scene,black,b-1,black,black_y,1,1,217);ht_limb(ht_scene,white,b-1,white,white_y,1,1,191);
 ht_rect(ht_scene,black-2,black_y-3,5,6,250);ht_rect(ht_scene,black-1,black_y-2,1,3,131);
 ht_rect(ht_scene,white-2,white_y-3,5,6,35);ht_limb(ht_scene,white-1,white_y-2,white+1,white_y,1,0,174);
 if(g->verdict){int locked=g->verdict==1?white:black;ht_limb(ht_scene,locked-4,b-9,locked+4,b-9,1,1,230);}
 ht_rect(ht_scene,x-18,top,36,34,239);ht_rect(ht_scene,x-15,top+3,30,28,89);
 ht_rect(ht_scene,x-1,top+2,2,30,224);
 /* Back/master face. The dated seal is represented without invented numerals. */
 if(g->verdict==1){
  ht_rect(ht_scene,x-14,top+5,12,23,52);
  for(int n=0;n<5;++n)ht_rect(ht_scene,x-13,top+9+n*3,10,1,129);
  ht_limb(ht_scene,x-12,top+7,x-12,top+25,1,0,173);ht_disk(ht_scene,x-5,top+8,2,192);
  ht_rect(ht_scene,x-16,top-7,16,7,218);
 }else{
  ht_rect(ht_scene,x-14,top+4,12,25,212);
  if(!g->verdict){ht_rect(ht_scene,x-6,top+8,3,15,98);ht_rect(ht_scene,x-8,top+10,6,2,167);}
 }
 /* Live/front face. Breaking the feed shutters it; completion exposes the pen. */
 if(g->verdict==1)ht_rect(ht_scene,x+2,top+4,12,25,232);
 else{
  ht_rect(ht_scene,x+3,b-4,11,3,60);
  unsigned phase=g->verdict==2?(g->ticks/6)%16:8;
  int nib=x+8+(int)(phase<8?phase:15-phase)/3,ny=b-6+(g->verdict==2?1:0);
  ht_limb(ht_scene,x+5,top+7,x+12,top+14,1,1,166);ht_limb(ht_scene,x+12,top+14,nib,ny-3,1,1,160);
  ht_disk(ht_scene,x+5,top+7,2,191);ht_disk(ht_scene,x+12,top+14,2,187);
  ht_limb(ht_scene,nib,ny-3,nib,ny,1,0,230);ht_disk(ht_scene,nib,ny,1,240);
  if(g->verdict==2){ht_rect(ht_scene,x+7,b-3,6,1,197);ht_rect(ht_scene,x+1,top-9,15,8,106);ht_limb(ht_scene,x+2,top-9,x+15,top-9,1,1,214);}
 }
 /* Lower smudges move with the raised live pane; face-height glass is clear. */
 int glass_y=g->verdict==2?top-5:b-9;
 for(int n=0;n<4;++n)ht_limb(ht_scene,x+4+n*2,glass_y,x+5+n*2,glass_y+2,1,0,126);
 if(!g->verdict){int reflection=ht_clamp((g->x/256-HT_PUZZLE_GATE)/12,-3,3);ht_rect(ht_scene,x+5+reflection,top+4,1,12,55);}
 ht_disk(ht_scene,x,top-8,4,235);if(g->verdict!=1)ht_disk(ht_scene,x,top-8,2,0);
 ht_checkpoint();
}
