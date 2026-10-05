#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
int main(void) {
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*gray=malloc(HT_NATIVE_PIXELS),*a=malloc(HT_NATIVE_PIXELS/8),*b=malloc(HT_NATIVE_PIXELS/8);
 assert(mem&&gray&&a&&b);ht_bind(mem);ht_bind_native(mem);
 for(int mode=0;mode<3;++mode)for(int angle=124;angle<=4096;angle+=1324) {
  memset(&ht,0,sizeof(ht));ht.level=0;ht_select_level(0);ht_spawn(true);
  ht_world_scale=256;ht_native_active=mode!=0;ht_native_foreground_half_y=mode==2;
  ht_scene=mode?ht_native_a:ht_scene_low;
  int size=mode?HT_NATIVE_PIXELS:HT_PIXELS;
  int dx=312*ht_forest_log_sine(angle)/256,dy=-312*ht_forest_log_sine(4096-angle)/256;
  memset(ht_scene,0,size);ht_game saved=ht;
  ht_forest_sawn_roots(&ht,200,210,dx,dy);assert(!memcmp(&ht,&saved,sizeof(ht)));
  memcpy(gray,ht_scene,size);ht_pack_mono(a,120);
  unsigned kerf=0,wood=0;for(int i=0;i<size;++i){kerf+=gray[i]==139;wood+=gray[i]==158;}
  assert(kerf && wood);
  memset(ht_scene,0,size);ht.traversal.forest_log_falling=true;saved=ht;
  ht_forest_sawn_roots(&ht,200,210,dx,dy);assert(!memcmp(&ht,&saved,sizeof(ht)));
  ht_pack_mono(b,120);assert(memcmp(a,b,HT_NATIVE_PIXELS/8));
  memcpy(gray,ht_scene,size);ht_forest_sawn_roots(&ht,200,210,dx,dy);assert(!memcmp(gray,ht_scene,size));
 }
 free(b);free(a);free(gray);free(mem);
 puts("Sawn roots: three raster modes, fixed kerf, rotating split wood, separated connection and immutable game state PASS");
}
