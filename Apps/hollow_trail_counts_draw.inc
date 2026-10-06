/* Chapter VII: the existing garden-label find owns the clipped count sheets. */
static int ht_counts_x(void){return ht_evidence_x(6,1);}
static int ht_counts_floor(void){return ht_land_height(6,4,ht_counts_x());}
static bool ht_counts_on_wire(unsigned stage,unsigned tick){return stage==0 || (stage==1 && tick<64);}
static bool ht_counts_under_pot(unsigned stage,unsigned tick){return stage==5 || (stage==4 && tick>=128);}
static void ht_counts_small_sheet(int x,int y){
 ht_rect(ht_scene,x,y,12,13,45);ht_limb(ht_scene,x,y,x+11,y,0,0,112);
 for(int n=0;n<3;++n)ht_rect(ht_scene,x+2,y+3+n*3,7,1,133);
 ht_limb(ht_scene,x+5,y+5,x+9,y+9,0,0,214);
}
static void ht_counts_room(const ht_game *g){
 if(g->level!=6)return;
 int x=ht_counts_x()-g->camera/256,f=ht_counts_floor()-g->camera_y/256;
 if(x+48< -ht_view_margin() || x-160>HT_W+ht_view_margin())return;
 /* Last clear pane; the neighbouring lower glass is still misted. */
 ht_rect(ht_scene,x-119,f-85,81,83,181);ht_rect(ht_scene,x-116,f-82,75,78,101);
 ht_rect(ht_scene,x-116,f-82,28,78,57);ht_rect(ht_scene,x-89,f-84,3,82,205);
 ht_limb(ht_scene,x-114,f-78,x-94,f-64,1,0,79);
 for(int n=0;n<16;++n){int xx=x-82+(n*17)%39,yy=f-77+(n*29)%68;ht_rect(ht_scene,xx,yy,1,3,48);}
 ht_limb(ht_scene,x-144,f-94,x-125,f-110,1,1,182);ht_limb(ht_scene,x-125,f-110,x-88,f-119,1,1,184);
 ht_limb(ht_scene,x-88,f-119,x-42,f-111,1,1,184);ht_limb(ht_scene,x-42,f-111,x+37,f-70,1,1,184);
 ht_limb(ht_scene,x-144,f-94,x-144,f,1,1,185);
 /* Narrow slates stand at the end of living rows, below the count wire. */
 for(int row=0;row<2;++row){int xx=x-57+row*38;
  ht_rect(ht_scene,xx-22,f-3,31,3,199);
  for(int n=0;n<4;++n){int px=xx-19+n*7;ht_limb(ht_scene,px,f-4,px+1,f-13-n%2*3,1,1,210);ht_limb(ht_scene,px+1,f-10,px-3,f-14,2,0,164);ht_limb(ht_scene,px+1,f-10,px+5,f-13,2,0,157);}
  ht_rect(ht_scene,xx+11,f-14,7,13,217);ht_text_color(xx+12,f-12,"7",1,62);
 }
 ht_limb(ht_scene,x-26,f-32,x+22,f-32,0,0,228);ht_limb(ht_scene,x-26,f-34,x-26,f-4,1,1,192);ht_limb(ht_scene,x+22,f-34,x+22,f-4,1,1,192);
 if(ht_counts_on_wire(g->counts_view_stage,g->counts_view_tick)){ht_counts_small_sheet(x-16,f-27);ht_counts_small_sheet(x+1,f-27);}
 ht_rect(ht_scene,x-12,f-33,3,7,231);ht_rect(ht_scene,x+5,f-33,3,7,231);
 /* Empty pot on a low bench: the counts are returned beneath its base. */
 ht_rect(ht_scene,x-7,f-12,36,3,209);ht_rect(ht_scene,x-5,f-9,3,9,197);ht_rect(ht_scene,x+24,f-9,3,9,197);
 if(ht_counts_under_pot(g->counts_view_stage,g->counts_view_tick)){ht_rect(ht_scene,x-5,f-14,22,2,47);ht_rect(ht_scene,x-3,f-16,21,2,57);}
 int lift=0;if(g->counts_view_stage==4 && g->counts_view_tick>=96 && g->counts_view_tick<144)lift=ht_min((int)g->counts_view_tick-96,144-(int)g->counts_view_tick)/3;
 ht_triangle(ht_scene,x+4,f-27-lift,x+23,f-27-lift,x+20,f-13-lift,177);ht_triangle(ht_scene,x+4,f-27-lift,x+20,f-13-lift,x+8,f-13-lift,193);
 ht_limb(ht_scene,x+4,f-27-lift,x+23,f-27-lift,2,2,212);ht_limb(ht_scene,x+7,f-27-lift,x+20,f-27-lift,1,1,245);
 ht_checkpoint();
}
