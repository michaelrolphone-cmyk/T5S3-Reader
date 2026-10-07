/* The corrected-map evidence belongs to the cabin beyond the trestle. */
#define HT_CABIN_PICKUP_TICKS 72u
#define HT_CABIN_UNROLL_START 72u
#define HT_CABIN_UNROLL_TICKS 96u
static int ht_cabin_x(void){return ht_evidence_x(3,1);}
static int ht_cabin_floor(void){return ht_land_height(3,4,ht_cabin_x());}
static void ht_signal_cabin_room(const ht_game *g){
 if(g->level!=3)return;
 int x=ht_cabin_x()-g->camera/256,f=ht_cabin_floor()-g->camera_y/256;
 if(x+64< -ht_view_margin() || x-64>HT_W+ht_view_margin())return;
 ht_rect(ht_scene,x-59,f-80,120,80,149);
 ht_triangle(ht_scene,x-66,f-80,x+2,f-97,x+67,f-80,212);
 ht_limb(ht_scene,x-65,f-80,x+65,f-80,2,2,228);
 for(int n=0;n<7;++n)ht_rect(ht_scene,x-56,f-74+n*10,114,1,169);
 ht_rect(ht_scene,x-56,f-78,4,78,223);ht_rect(ht_scene,x+55,f-78,4,78,223);
 /* A narrow window looks down three roads, without moving the real puzzle. */
 ht_rect(ht_scene,x-8,f-67,53,38,219);ht_rect(ht_scene,x-5,f-64,47,32,36);
 for(int road=0;road<3;++road){int foot=x-3+road*21;
  ht_limb(ht_scene,x+18,f-60,foot,f-34,1,1,153);ht_limb(ht_scene,x+20,f-60,foot+4,f-34,1,1,153);
 }
 ht_rect(ht_scene,x+17,f-65,2,33,202);ht_rect(ht_scene,x-7,f-32,52,3,218);
 ht_rect(ht_scene,x-42,f-57,31,31,190);
 if(g->cabin_view_stage!=2){
  ht_rect(ht_scene,x-39,f-54,25,26,58);
  ht_limb(ht_scene,x-36,f-46,x-18,f-42,1,1,170);ht_limb(ht_scene,x-33,f-36,x-19,f-47,1,1,167);
  ht_limb(ht_scene,x-29,f-45,x-23,f-50,1,1,91);ht_rect(ht_scene,x-36,f-33,17,1,142);
 }
 /* Numbered brass ovals and worn handles remain the same objects when bare. */
 for(int n=0;n<3;++n){int lever=x-10+n*22;
  ht_rect(ht_scene,lever-5,f-4,10,4,221);ht_limb(ht_scene,lever,f-4,lever,f-28,2,2,210);
  ht_rect(ht_scene,lever-4,f-30,8,5,235);ht_rect(ht_scene,lever-2,f-30,3,2,110);
  ht_disk(ht_scene,lever,f-13,4,146);char number[2]={(char)('1'+n),0};ht_text_color(lever-2,f-16,number,1,241);
  if(n==1){
   bool shirt=(g->packet&HT_PACKET_DOG) || (g->cabin_view_stage==7 && g->cabin_view_tick>=176);
   bool paper=!shirt && (g->cabin_view_stage<3 || g->cabin_view_stage==6 || (g->cabin_view_stage==3 && g->cabin_view_tick<HT_CABIN_UNROLL_START) || (g->cabin_view_stage==5 && g->cabin_view_tick>=112) || (g->cabin_view_stage==7 && g->cabin_view_tick<48));
   if(paper){ht_rect(ht_scene,lever-3,f-30,6,7,55);ht_limb(ht_scene,lever-3,f-28,lever+3,f-26,1,1,127);}
   if(shirt){ht_limb(ht_scene,lever-3,f-29,lever+3,f-27,1,1,91);ht_limb(ht_scene,lever+2,f-27,lever+3,f-22,1,0,103);}
  }
 }
 ht_rect(ht_scene,x-56,f,112,4,215);ht_checkpoint();
}
