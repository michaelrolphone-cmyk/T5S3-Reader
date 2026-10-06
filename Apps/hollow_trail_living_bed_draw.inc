/* The first tended bed precedes the existing movable glasshouse crate. */
#define HT_BED_X 232
static int ht_bed_floor(void){return ht_land_height(6,0,HT_BED_X);}
static unsigned ht_bed_bend(unsigned stage,unsigned tick){
 if(stage==2)return ht_min(tick,48)*256/48;
 if(stage==3)return 256;
 if(stage==4)return tick<128?256:256-ht_min(tick-128,64)*256/64;
 return 0;
}
static bool ht_bed_dust(unsigned stage,unsigned tick){return stage==3 || (stage==2 && tick>=48) || (stage==4 && tick<120);}
static void ht_living_bed(const ht_game *g){
 if(g->level!=6)return;
 int x=HT_BED_X-g->camera/256,f=ht_bed_floor()-g->camera_y/256;
 if(x+86< -ht_view_margin() || x-84>HT_W+ht_view_margin())return;
 /* Ribs and surviving pane fragments shelter the actual ground-level bed. */
 static const int8_t arch[][2]={{0,-56},{8,-67},{21,-76},{32,-78},{45,-74},{58,-65},{65,-57}};
 for(int n=0;n<3;++n){int xx=x-77+n*73;ht_limb(ht_scene,xx,f,xx,f-56,1,1,165);for(unsigned k=0;k+1<sizeof(arch)/sizeof(arch[0]);++k)ht_limb(ht_scene,xx+arch[k][0],f+arch[k][1],xx+arch[k+1][0],f+arch[k+1][1],1,1,165);}
 ht_limb(ht_scene,x-45,f-78,x+101,f-78,1,1,162);
 ht_triangle(ht_scene,x-73,f-54,x-46,f-72,x-50,f-48,52);
 ht_limb(ht_scene,x-73,f-54,x-50,f-48,1,0,184);
 ht_rect(ht_scene,x-63,f-4,127,5,203);
 for(int n=0;n<3;++n){int yy=f-3-n*4;ht_limb(ht_scene,x-59,yy,x+61,yy,1,0,94);}
 /* Strings, shallow root channels, and a broken pot around a new shoot. */
 ht_limb(ht_scene,x-53,f-25,x+57,f-25,1,0,157);
 ht_limb(ht_scene,x-53,f-27,x-53,f-3,1,1,200);ht_limb(ht_scene,x+57,f-27,x+57,f-3,1,1,200);
 for(int n=0;n<5;++n){int xx=x-43+n*23;
  ht_limb(ht_scene,xx,f-3,xx+4,f-13,1,1,186);ht_limb(ht_scene,xx+4,f-13,xx-2,f-18,1,0,150);
  ht_limb(ht_scene,xx+3,f-10,xx+9,f-15,1,0,150);ht_limb(ht_scene,xx-6,f-1,xx-2,f-5,1,0,72);
 }
 unsigned bend=ht_bed_bend(g->bed_view_stage,g->bed_view_tick);
 int tip=f-17+(int)bend*5/256;
 ht_limb(ht_scene,x,f-2,x+2,f-12,1,1,203);
 ht_triangle(ht_scene,x+2,f-12,x+12,tip,x+8,tip+5,153);
 ht_limb(ht_scene,x+2,f-12,x+11,tip+2,1,0,213);
 if(ht_bed_dust(g->bed_view_stage,g->bed_view_tick)){ht_limb(ht_scene,x+7,tip+1,x+10,tip+2,1,1,45);ht_rect(ht_scene,x+7,tip+1,1,1,151);}
 ht_triangle(ht_scene,x+35,f-2,x+38,f-15,x+44,f-13,191);ht_triangle(ht_scene,x+48,f-2,x+48,f-11,x+53,f-3,187);
 ht_limb(ht_scene,x+44,f-3,x+43,f-11,1,1,163);ht_limb(ht_scene,x+43,f-11,x+46,f-14,1,0,124);
 /* The watering can rests on bricks; a root reaches its wet circle. */
 ht_rect(ht_scene,x+66,f-5,9,5,154);ht_rect(ht_scene,x+77,f-5,9,5,154);
 ht_rect(ht_scene,x+69,f-19,14,14,184);ht_limb(ht_scene,x+82,f-15,x+91,f-21,2,1,183);
 ht_limb(ht_scene,x+69,f-18,x+65,f-15,1,1,183);ht_limb(ht_scene,x+65,f-15,x+69,f-9,1,1,183);
 ht_limb(ht_scene,x+64,f-1,x+79,f-1,1,1,82);ht_limb(ht_scene,x+54,f-1,x+68,f-2,1,0,193);
 ht_checkpoint();
}
