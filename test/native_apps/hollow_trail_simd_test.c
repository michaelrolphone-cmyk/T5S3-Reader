/* Host verification covers the portable instruction model and shared row
 * scheduler. The S3 opcode implementation is validated on-device at startup. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned checkpoints;
static void service(void){++checkpoints;}
static void runtime_tables(void){
 /* App aligns its arena even when a heap allocation or relocated ELF section
  * lands at a different byte offset. Tables must remain in that arena. */
 uint8_t *raw=malloc(HT_MEMORY+64);assert(raw);
 const unsigned threshold[4][4]={{3,51,15,63},{35,19,47,31},{11,59,7,55},{43,27,39,23}};
 const unsigned offsets[4]={0,192,128,64};
 for(unsigned offset=0;offset<16;++offset){
  memset(raw,0xa5,HT_MEMORY+64);
  uint8_t *base=(uint8_t *)(((uintptr_t)(raw+offset)+15u)&~(uintptr_t)15u);
  ht_bind(base);
  assert(!((uintptr_t)ht_simd_constants&15u) && !((uintptr_t)ht_expand_constants&15u));
  assert((uint8_t *)ht_simd_constants==ht_recon_scene+HT_RECON_PIXELS);
  assert((uint8_t *)ht_expand_constants+48==base+HT_MEMORY);
  for(unsigned phase=0;phase<4;++phase)for(unsigned lane=0;lane<16;++lane){
   assert(ht_simd_constants[phase][0][lane]==128 && ht_simd_constants[phase][1][lane]==127);
   for(unsigned plane=0;plane<4;++plane)
    assert(ht_simd_constants[phase][plane+2][lane]==((threshold[phase][lane&3]+offsets[plane]-1)^128u));
  }
  for(unsigned lane=0;lane<16;++lane)
   assert(ht_expand_constants[0][lane]==128 && ht_expand_constants[1][lane]==127 && ht_expand_constants[2][lane]==79);
  for(uint8_t *p=raw;p<base;++p)assert(*p==0xa5);
  for(uint8_t *p=base+HT_MEMORY;p<raw+HT_MEMORY+64;++p)assert(*p==0xa5);
 }
 uint8_t got[3]={1,7,3},want[3]={1,2,3};
 assert(!ht_simd_test_equal(got,want,3,17,2));
 assert(ht_simd_test_pattern==17 && ht_simd_test_phase==2 && ht_simd_test_byte==1);
 assert(ht_simd_test_expected==2 && ht_simd_test_actual==7);
 assert(ht_simd_test_equal(want,want,3,0,0));
 free(raw);
}
int main(void){
 runtime_tables();
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
