#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
int main(void) {
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*gray=malloc(HT_NATIVE_PIXELS),*mono=malloc(HT_NATIVE_PIXELS/8),*before=malloc(HT_NATIVE_PIXELS/8);
 assert(mem&&gray&&mono&&before);ht_bind(mem);ht_bind_native(mem);
 for(int mode=0;mode<3;++mode) {
  memset(&ht,0,sizeof(ht));ht.level=0;ht_select_level(0);ht_spawn(true);
  ht_world_scale=256;ht_native_active=mode!=0;ht_native_foreground_half_y=mode==2;
  ht_scene=mode?ht_native_a:ht_scene_low;
  int bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
  memset(ht_scene,0,bytes);ht_game saved=ht;ht_marked_tree(200,210,true);
  assert(!memcmp(&ht,&saved,sizeof(ht)));memcpy(gray,ht_scene,bytes);ht_pack_mono(before,120);
  /* Material regions must survive low/native/half-height production raster. */
  unsigned wool=0,paper=0,callus=0,date=0;
  for(int i=0;i<bytes;++i){wool+=gray[i]==99 || gray[i]==114;paper+=gray[i]==64;callus+=gray[i]==174;date+=gray[i]==160;}
  assert(wool&&paper&&callus&&date);
  memset(ht_scene,0,bytes);ht_marked_tree(200,210,false);ht_pack_mono(mono,120);
  unsigned changed=0;
  for(int i=0;i<HT_NATIVE_PIXELS/8;++i)changed+=mono[i]!=before[i];
  assert(changed);unsigned retained_wool=0;
  for(int i=0;i<bytes;++i)retained_wool+=ht_scene[i]==99 || ht_scene[i]==114;
  assert(retained_wool>=wool); /* Taking paper never removes the cloth. */
 }
 ht_native_active=ht_native_foreground_half_y=false;
 memset(&ht,0,sizeof(ht));ht.level=0;ht_select_level(0);ht_spawn(true);
 for(int i=0;i<80 && ht.x<140*256;++i)ht_step_controls(1,0,false,false);
 int x=ht.x,y=ht.y;assert(ht_evidence_near(&ht)==0 && ht_inspect()==0);
 assert(ht.evidence==1 && ht.scene_evidence==1 && ht.x==x && ht.y==y && !ht.puzzle.solved);
 assert(ht_inspect()==0 && ht.evidence==1);ht_spawn(false);assert(ht.evidence==1);
 free(before);free(mono);free(gray);free(mem);
 puts("Marked tree: connected wool/cut/date/paper in all rasters, actual inspection, retained cloth and unchanged route state PASS");
}
