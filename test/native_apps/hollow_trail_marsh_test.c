/* Scenery must leave the real ferry, bank, waiting evidence and lock route intact. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "hollow_trail_route_walk.inc"
static unsigned checkpoints;
static void service(void){++checkpoints;}
int main(void) {
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
 uint8_t *bits=malloc(HT_NATIVE_PIXELS/8),*other=malloc(HT_NATIVE_PIXELS/8);
 assert(mem && copy && bits && other);ht_bind(mem);ht_bind_native(mem);
 assert(ht_marsh_cloud(0)==0 && ht_marsh_cloud(512)==256 && ht_marsh_cloud(1024)==0);
 for(unsigned tick=0;tick<4096;++tick){
  assert(ht_abs(ht_marsh_cloud(tick+1)-ht_marsh_cloud(tick))<=4);
  assert(ht_marsh_cloud(tick)==ht_marsh_cloud(tick+1024));
 }
 /* Actual input-only route includes the untouched rowboat, real channel jump,
  * all three original discoveries, and all lock controls before the exit. */
 memset(&ht,0,sizeof(ht));ht.level=4;ht_select_level(4);ht_spawn(true);
 unsigned boats=0,rendered=0;
 for(unsigned step=0;step<16000 && ht.level==4;++step){
  walk_route_tick();if(ht.traversal.mode==HT_BOAT)++boats;
  if(ht.level==4 && ht.grounded && ((ht.x/256>=1650 && rendered==0)||(ht.x/256>=2730 && rendered==1))){
   for(unsigned mode=0;mode<2;++mode){
    ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
    ht_service=service;checkpoints=0;ht_render_scene();assert(checkpoints>0 && checkpoints<4000);
    size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,120);
    ht_render_scene();ht_pack_mono(other,120);
    assert(!memcmp(copy,ht_scene,bytes) && !memcmp(bits,other,HT_NATIVE_PIXELS/8));
    assert(!memcmp(&ht,&frozen,sizeof(ht)));
   }
   ++rendered;
  }
 }
 assert(ht.level==5 && !ht.deaths && boats>80 && rendered==2);
 assert((ht.evidence&(7u<<12))==(7u<<12));ht_service=NULL;
 /* At identical positions, sky reflection hides fixed underwater wood. The
  * physical bank's top 19 rows remain byte-identical in every raster path. */
 for(unsigned mode=0;mode<3;++mode){
  ht_bind(mem);ht_bind_native(mem);ht.level=4;ht_spawn(true);ht.camera=1770*256;ht.camera_y=40*256;
  ht_world_scale=256;ht_native_active=mode!=0;ht_native_foreground_half_y=mode==2;
  ht_scene=mode?ht_native_a:ht_scene_low;size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
  memset(ht_scene,255,bytes);ht.ticks=0;ht_game frozen=ht;ht_marsh_bank(&ht);
  assert(!memcmp(&ht,&frozen,sizeof(ht)));memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,120);
  memset(ht_scene,255,bytes);ht.ticks=512;frozen=ht;ht_marsh_bank(&ht);ht_pack_mono(other,120);
  assert(!memcmp(&ht,&frozen,sizeof(ht)) && memcmp(bits,other,HT_NATIVE_PIXELS/8));
  for(int wx=1850;wx<2220;++wx){int parcel=wx<2130?5:6,top=ht_surface_at(&ht,parcel,wx)-40;
   for(int dy=-4;dy<19;++dy)for(int sample=0;sample<(mode?2:1);++sample){
    int at=(mode?2:1)*(top+dy)*(mode?HT_NATIVE_W:HT_W)+(wx-1770)*(mode?2:1)+sample;
    assert(copy[at]==ht_scene[at]);
   }
  }
 }
 /* Grotto and unrelated chapters are strict no-ops, even at camera extremes. */
 for(unsigned level=0;level<HT_LEVELS;++level)for(int cx=-120;cx<=2940;cx+=153){
  if(level==4 && cx>901)continue;
  ht_bind(mem);ht_bind_native(mem);ht.level=level;ht_spawn(true);ht.camera=cx*256;
  ht_native_active=true;ht_native_foreground_half_y=false;ht_scene=ht_native_a;ht_world_scale=176;
  memset(ht_scene,81,HT_NATIVE_PIXELS);memcpy(copy,ht_scene,HT_NATIVE_PIXELS);ht_game frozen=ht;
  ht_marsh_backdrop(&ht);ht_marsh_bank(&ht);ht_marsh_foreground(&ht);
  assert(!memcmp(copy,ht_scene,HT_NATIVE_PIXELS) && !memcmp(&ht,&frozen,sizeof(ht)));
 }
 free(other);free(bits);free(copy);free(mem);
 puts("Marsh scenery: actual ferry-to-quarry route, evidence/lock controls, immutable rendering, cloud cycle, three rasters, stable contact edge and unrelated/grotto no-ops PASS");
}
