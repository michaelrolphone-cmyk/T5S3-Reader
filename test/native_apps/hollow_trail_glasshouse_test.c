/* Arrival scenery must remain a pure view over the original route. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "hollow_trail_route_walk.inc"
static unsigned services;
static void service(void){++services;}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS),*bits=malloc(HT_NATIVE_PIXELS/8),*packed=malloc(HT_NATIVE_PIXELS/8);
 assert(memory && copy && bits && packed);ht_bind(memory);ht_bind_native(memory);
 memset(&ht,0,sizeof(ht));ht.level=6;ht_select_level(6);ht_spawn(true);
 /* Real horizontal input crosses and returns through the gallery mouth.
  * Material changes cannot create a ledge, collision, collectible or gate. */
 for(unsigned pass=0;pass<2;++pass){
  int target=pass?95:270;
  for(unsigned i=0;i<2000 && ht_abs(ht.x/256-target)>2;++i)ht_step_controls(pass?-1:1,0,false,true);
  assert(ht_abs(ht.x/256-target)<=2 && !ht.deaths && ht.grounded);
  assert(ht.y/256==ht_surface_at(&ht,0,ht.x/256));
  assert(!ht.evidence && !ht.puzzle.solved && ht.traversal.mode==HT_FREE);
 }
 /* Frozen-camera output is deterministic in both native and low paths,
  * with widening, close view and a reverse-facing return through the mouth. */
 static const int positions[]={95,150,218,315,650};
 for(unsigned mode=0;mode<2;++mode)for(unsigned n=0;n<5;++n)for(unsigned wide=0;wide<2;++wide){
  ht_bind(memory);ht_bind_native(memory);ht.level=6;ht_spawn(true);
  ht.x=positions[n]*256;ht.y=ht_surface_at(&ht,n==4?1:0,positions[n])*256;
  ht.camera=(positions[n]-200)*256;ht.camera_y=(ht.y/256-180)*256;
  ht.facing=n==0?-1:1;ht.intimacy=wide?0:384;ht.vista=wide?256:0;
  ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;
  ht_game before=ht;services=0;ht_service=service;ht_render_scene();
  assert(services>0 && services<3000);size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
  memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,HT_NATIVE_W/8);ht_render_scene();ht_pack_mono(packed,HT_NATIVE_W/8);
  assert(!memcmp(copy,ht_scene,bytes) && !memcmp(bits,packed,HT_NATIVE_PIXELS/8));
  assert(!memcmp(&ht,&before,sizeof(ht)));
 }
 ht_service=NULL;
 /* Arrival view is pure for every chapter, and vanishes outside chapter VII. */
 for(unsigned level=0;level<HT_LEVELS;++level){
  ht_bind(memory);ht_bind_native(memory);ht.level=level;ht_spawn(true);
  ht_native_active=true;ht_native_foreground_half_y=true;ht_scene=ht_native_a;ht_world_scale=256;
  memset(ht_scene,81,HT_NATIVE_PIXELS);memcpy(copy,ht_scene,HT_NATIVE_PIXELS);ht_game before=ht;
  ht_gallery_wall(&ht);ht_gallery_floor(&ht);ht_glasshouse_arrival(&ht);
  assert(!memcmp(&ht,&before,sizeof(ht)));
  if(level!=6)assert(!memcmp(copy,ht_scene,HT_NATIVE_PIXELS));
  else assert(memcmp(copy,ht_scene,HT_NATIVE_PIXELS));
 }
 /* The existing complete route still operates the crate, both mirror stages
  * and all three original evidence sites with no deaths. */
 ht_bind(memory);ht_bind_native(memory);memset(&ht,0,sizeof(ht));ht.level=6;ht_select_level(6);ht_spawn(true);walk_level=999;
 for(unsigned i=0;i<20000 && ht.level==6;++i)walk_route_tick();
 assert(ht.level==7 && !ht.deaths);
 free(packed);free(bits);free(copy);free(memory);
 puts("Glasshouse arrival: original support crossing/return, pure chapter-scoped scenery, both raster paths, deterministic packed mono, bounded render service and complete mirror/crate route PASS");
}
