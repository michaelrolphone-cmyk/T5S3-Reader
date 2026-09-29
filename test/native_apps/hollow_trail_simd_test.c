/* Host verification covers the portable instruction model and shared row
 * scheduler. The S3 opcode implementation is validated on-device at startup. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned checkpoints;
static void service(void){++checkpoints;}
int main(void){
 for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b)
  assert((ht_simd_mean((uint8_t)(a^128),(uint8_t)(b^128))^128)==(a+b)/2);
 uint8_t *mem=malloc(HT_MEMORY),*reference=malloc(125*540),*guard=malloc(125*540+32);
 assert(mem&&reference&&guard);ht_bind(mem);ht_service=service;
 for(int pattern=0;pattern<6;++pattern){
  for(int i=0;i<HT_PIXELS;++i)ht_scene[i]=(uint8_t)(pattern==0?0:pattern==1?255:
      pattern==2?(i%HT_W)*255/(HT_W-1):pattern==3?((i/HT_W+i%HT_W)&1)*255:
      (int)(ht_hash((unsigned)i+pattern*97)&255));
  for(int framed=0;framed<2;++framed){
   if(framed)ht_vignette();else ht_framed=false;
   for(int stride=120;stride<=125;stride+=5)for(int offset=0;offset<2;++offset){
    memset(guard,0xa5,125*540+32);uint8_t *out=guard+16+offset;
    ht_pack_mono_legacy(reference,stride);ht_pack_mono_simd(out,stride);
    for(int y=0;y<540;++y){
     assert(!memcmp(reference+y*stride,out+y*stride,120));
     for(int x=120;x<stride;++x)assert(out[y*stride+x]==0xa5);
    }
    for(int i=0;i<16+offset;++i)assert(guard[i]==0xa5);
    for(int i=16+offset+stride*540;i<125*540+32;++i)assert(guard[i]==0xa5);
   }
  }
 }
 for(unsigned level=0;level<HT_LEVELS;++level)for(int view=0;view<2;++view){
  ht.level=level;ht_spawn(true);ht.camera=view*1100*256;ht.x=(view*1100+190)*256;
  ht.vista=view?256:0;ht.sway_phase=512;ht.rotation_phase=256u<<8;ht_render_scene();
  ht_pack_mono_legacy(reference,120);ht_pack_mono_simd(guard,120);
  assert(!memcmp(reference,guard,64800));
 }
 ht_output_mode=HT_OUTPUT_SIMD;ht_simd_ready=false;
 ht_pack_mono(reference,120);ht_pack_mono_legacy(guard,120);assert(!memcmp(reference,guard,64800));
 assert(checkpoints>0);free(guard);free(reference);free(mem);
 puts("SIMD portable model: all byte averages, phases, boundaries, guards, fallback and all chapters PASS; S3 execution requires device self-test");
}
