#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
int main(void) {
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*a=malloc(HT_NATIVE_PIXELS/8),*b=malloc(HT_NATIVE_PIXELS/8);
 assert(mem&&copy&&a&&b);ht_bind(mem);ht_bind_native(mem);
 for(int mode=0;mode<3;++mode) {
  memset(&ht,0,sizeof(ht));ht.level=1;ht_select_level(1);ht_spawn(true);
  ht.camera=500*256;ht.camera_y=-180*256;ht_world_scale=256;
  ht_native_active=mode!=0;ht_native_foreground_half_y=mode==2;ht_scene=mode?ht_native_a:ht_scene_low;
  int bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS,raster=mode?2:1;
  memset(ht_scene,0,bytes);ht_game frozen=ht;ht_city_vanished_house(&ht);assert(!memcmp(&ht,&frozen,sizeof(ht)));
  unsigned room0=0,room1=0,paper=0,holes=0;
  for(int i=0;i<bytes;++i){room0+=ht_scene[i]==100;room1+=ht_scene[i]==87;paper+=ht_scene[i]==117 || ht_scene[i]==120;holes+=ht_scene[i]==203;}
  assert(room0 && room1 && paper && holes);ht_pack_mono(a,120);
  memcpy(copy,ht_scene,bytes);ht.ticks+=500;ht_city_vanished_house(&ht);assert(!memcmp(copy,ht_scene,bytes));
  /* Both bare rooms survive the production mono packer. */
  for(int i=0;i<bytes;++i)if(ht_scene[i]==100 || ht_scene[i]==87)ht_scene[i]=151;
  ht_pack_mono(b,120);
  for(int room=0;room<2;++room) {
   unsigned changed=0;int left=(185+room*98+8)*2,right=(185+room*98+79)*2;
   for(int y=85*2;y<107*2;++y)for(int x=left;x<right;++x)
    changed+=((a[y*120+x/8]^b[y*120+x/8])&(0x80u>>(x&7)))!=0;
   assert(changed);
  }
  /* The existing solid roof occludes the new wall's lower continuation. */
  memcpy(ht_scene,copy,bytes);ht_draw_land(&ht,2,0,630,900,-60);memcpy(copy,ht_scene,bytes);
  memset(ht_scene,0,bytes);ht_draw_land(&ht,2,0,630,900,-60);
  for(int y=132*raster;y<200*raster;++y)for(int x=200*raster;x<380*raster;++x)
   assert(ht_scene[y*HT_W*raster+x]==copy[y*HT_W*raster+x]);
  memset(ht_scene,0,bytes);ht.level=0;ht_city_vanished_house(&ht);
  for(int i=0;i<bytes;++i)assert(!ht_scene[i]);
  ht.level=1;ht.camera=1300*256;ht_city_vanished_house(&ht);
  for(int i=0;i<bytes;++i)assert(!ht_scene[i]);
 }
 free(b);free(a);free(copy);free(mem);
 puts("Vanished house: two exposed rooms, sheltered paper, beam pockets, mono, route occlusion and immutable state PASS");
}
