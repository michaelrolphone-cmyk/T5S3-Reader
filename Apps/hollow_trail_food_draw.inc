/* First-house work area on the existing flat soil (250..480), behind the
 * walking strip. The foreground crate still travels from 330 to 452. */
#define HT_FOOD_X 373
#define HT_FOOD_CUPBOARD_X 426
static int ht_food_floor(void){return ht_land_height(6,0,HT_FOOD_X);}
static unsigned ht_food_open(unsigned stage,unsigned tick){
 if(stage<6)return 0;
 if(stage==6)return (unsigned)ht_clamp((int)tick-96,0,64)*256/64;
 if(stage==13)return 256-ht_min(tick,128)*256/128;
 return 256;
}
static void ht_food_cup(int x,int y){
 ht_rect(ht_scene,x-2,y-3,5,5,67);ht_limb(ht_scene,x-2,y-3,x+2,y-3,1,1,198);
 ht_limb(ht_scene,x+3,y-2,x+4,y-1,0,0,208);ht_limb(ht_scene,x+4,y-1,x+3,y+1,0,0,208);
}
static void ht_food_room(const ht_game *g){
 if(g->level!=6)return;
 int x=HT_FOOD_X-g->camera/256,f=ht_food_floor()-g->camera_y/256;
 if(x+104< -ht_view_margin() || x-10>HT_W+ht_view_margin())return;
 /* Thick return enters the soil. Felt binds a damp joint; sweat beads collect
  * on its cooler descending face, separate from the one slow tap drop. */
 ht_limb(ht_scene,x-15,f-7,x+48,f-7,3,3,195);ht_limb(ht_scene,x+48,f-45,x+48,f+2,3,3,195);
 ht_limb(ht_scene,x+42,f-45,x+48,f-45,3,3,195);
 for(int n=0;n<5;++n){ht_rect(ht_scene,x+46,f-40+n*6,1,2,48);ht_rect(ht_scene,x+51,f-35+n*5,1,2,65);}
 ht_limb(ht_scene,x+34,f-9,x+41,f-9,3,3,130);
 for(int n=0;n<4;++n){ht_rect(ht_scene,x+35+n*2,f-12,1,7,173);ht_rect(ht_scene,x+43+n*2,f-4+n%2,1,2,58);}
 /* Staved barrel, two hoops, open basin and the little mineral mound. */
 ht_rect(ht_scene,x+9,f-24,26,24,167);ht_limb(ht_scene,x+9,f-24,x+7,f-3,1,1,213);ht_limb(ht_scene,x+35,f-24,x+37,f-3,1,1,213);
 for(int n=0;n<5;++n)ht_limb(ht_scene,x+12+n*5,f-22,x+11+n*6,f-2,0,0,208);
 ht_rect(ht_scene,x+8,f-18,29,2,228);ht_rect(ht_scene,x+7,f-6,30,2,226);
 ht_limb(ht_scene,x+10,f-25,x+33,f-25,2,2,211);ht_limb(ht_scene,x+11,f-25,x+32,f-25,1,1,68);
 ht_triangle(ht_scene,x+24,f-25,x+28,f-31,x+33,f-25,82);ht_rect(ht_scene,x+28,f-31,1,2,37);
 ht_limb(ht_scene,x+42,f-45,x+29,f-45,2,2,188);ht_limb(ht_scene,x+29,f-45,x+29,f-41,1,1,188);ht_limb(ht_scene,x+33,f-49,x+39,f-49,1,1,186);
 unsigned drip=g->food_view_active?g->food_view_ambient:g->ticks&255u;
 if(drip>=240)ht_rect(ht_scene,x+29,f-40+(int)(drip-240)*9/16,1,2,50);
 /* Row of cups is a real reachable ledge next to the barrel. */
 ht_limb(ht_scene,x-3,f-26,x+10,f-26,1,1,179);ht_limb(ht_scene,x+1,f-26,x+10,f-18,1,1,179);
 bool carrying=g->food_view_active && ((g->food_view_stage==1 && g->food_view_tick>=32) || g->food_view_stage==2 || g->food_view_stage==3 || g->food_view_stage==4 || g->food_view_stage==5 || (g->food_view_stage==6 && g->food_view_tick<32));
 for(int n=0;n<3;++n)if(n || !carrying)ht_food_cup(x+n*5,f-29);
 /* Food cupboard beneath the workbench. It adds no collision surface. */
 ht_rect(ht_scene,x+51,f-29,51,4,193);ht_rect(ht_scene,x+53,f-25,3,25,209);ht_rect(ht_scene,x+97,f-25,3,25,209);
 ht_rect(ht_scene,x+58,f-24,36,23,205);ht_rect(ht_scene,x+60,f-22,32,18,79);
 ht_rect(ht_scene,x+74,f-18,8,11,116);ht_rect(ht_scene,x+74,f-20,8,3,208);
 for(int n=0;n<5;++n)ht_disk(ht_scene,x+76+n%2*3,f-16+n/2*3,1,190);
 ht_rect(ht_scene,x+62,f-9,9,2,139);
 bool taken=g->food_view_active && (g->food_view_stage>8 || (g->food_view_stage==8 && g->food_view_tick>=64));
 if(!taken)ht_rect(ht_scene,x+63,f-12,8,2,132);
 ht_rect(ht_scene,x+83,f-6,8,2,113);ht_limb(ht_scene,x+84,f-11,x+87,f-6,1,0,179);ht_limb(ht_scene,x+88,f-12,x+88,f-6,1,0,167);
 unsigned open=g->food_view_active?ht_food_open(g->food_view_stage,g->food_view_tick):0;
 int width=34-(int)open*48/256,edge=x+59+width;
 ht_rect(ht_scene,ht_min(x+59,edge),f-24,ht_abs(width)+1,23,155);ht_limb(ht_scene,edge,f-24,edge,f-2,1,1,211);
 if(open>128){for(int n=0;n<4;++n)ht_limb(ht_scene,edge+3,f-20+n*3,x+57,f-20+n*3,0,0,79);}
 else ht_disk(ht_scene,edge-3,f-13,1,227);
 ht_checkpoint();
}
