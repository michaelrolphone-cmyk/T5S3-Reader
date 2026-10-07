/* The Chapter VII papers remain at the existing third glasshouse find. */
static int ht_drawing_x(void){return ht_evidence_x(6,2);}
static int ht_drawing_floor(void){return ht_land_height(6,7,ht_drawing_x());}
static bool ht_drawing_on_wall(unsigned stage,unsigned tick){return !stage || (stage==1 && tick<64) || stage==8 || (stage==7 && tick>=64);}
static void ht_drawing_small_sheet(int x,int y,bool older){
 ht_rect(ht_scene,x,y,23,26,older?66:46);ht_limb(ht_scene,x+1,y+1,x+21,y+1,0,0,118);
 ht_limb(ht_scene,x+4,y+20,x+8,y+11,1,0,185);ht_limb(ht_scene,x+8,y+11,x+18,y+22,1,0,185);ht_disk(ht_scene,x+8,y+7,2,188);
}
static void ht_drawing_room(const ht_game *g){
 if(g->level!=6)return;
 int x=ht_drawing_x()-g->camera/256,f=ht_drawing_floor()-g->camera_y/256;
 if(x+66< -ht_view_margin() || x-62>HT_W+ht_view_margin())return;
 ht_rect(ht_scene,x-50,f-58,111,59,121);ht_rect(ht_scene,x-47,f-55,105,53,88);
 for(int row=0;row<4;++row){int y=f-13-row*13;ht_limb(ht_scene,x-46,y,x+57,y,0,0,128);for(int n=0;n<3;++n){int xx=x-43+n*35+(row&1)*12;ht_limb(ht_scene,xx,y,xx,y-12,0,0,116);}}
 ht_limb(ht_scene,x-54,f-59,x+64,f-59,1,1,174);
 {
  for(int i=0;i<2;++i){if(!i && !ht_drawing_on_wall(g->drawing_view_stage,g->drawing_view_tick))continue;int xx=x-12+i*29;ht_drawing_small_sheet(xx,f-40,i!=0);ht_rect(ht_scene,xx+8,f-42,5,4,215);}
 }
 /* Morning bars are local to the sheltered wall; neither receiver is changed. */
 ht_limb(ht_scene,x-45,f-52,x-21,f-3,2,1,53);ht_limb(ht_scene,x+12,f-53,x+36,f-3,2,1,56);
 ht_checkpoint();
}
