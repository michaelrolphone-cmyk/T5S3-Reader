/* Differential coverage for row integration. Host runs portable leaf oracles;
 * actual assembly is gated by a separate device startup self-test. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned checks;
static void service(void){++checks;}
static void mode(bool enabled){ht_simd_stage_ready=enabled?HT_OPT_SIMD_ALL:0;ht_expanded_ready=enabled;}
static void pattern(uint8_t *dst,int count,unsigned seed){
 for(int i=0;i<count;++i)dst[i]=(uint8_t)(seed<2?seed*255:ht_hash((unsigned)i+seed*97));
}
int main(void){
 uint8_t *mem=malloc(HT_MEMORY),*want=malloc(HT_PIXELS),*actual=malloc(HT_PIXELS);
 assert(mem&&want&&actual);ht_bind(mem);ht_service=service;
 /* Signed multiply >>8, add, unsigned multiply >>8: prove the original
  * special branches and fused lane formula agree, including negative deltas. */
 for(int fade=45;fade<=70;fade+=25)for(int a=0;a<256;++a)for(int b=0;b<256;++b)for(int r=0;r<=256;++r){
  int original=r==0?a:r==256?(b*(256-fade)>>8):
      (((a*256+(b-a)*r)>>8)*(256-((r*fade)>>8)))>>8;
  int fused=a+(((b-a)*r)>>8);fused=fused*(256-((r*fade)>>8))>>8;
  assert(fused==original);
 }
 /* The corrected 16-bit reciprocal is exact throughout every reachable sum,
  * and agrees with the existing 24-bit reciprocal (not rounded division). */
 for(unsigned radius=1;radius<=9;++radius){
  unsigned n=radius*2+1,k=(65536+n-1)/n,reciprocal=(0x1000000+n-1)/n;
  for(unsigned sum=0;sum<=255*n;++sum){
   unsigned q=(sum*k)>>16;q-=q*n>sum;
   assert(q==sum/n && q==ht_average((int)sum,reciprocal));
  }
 }
 for(unsigned seed=0;seed<8;++seed){
  pattern(ht_low_scene,HT_SCENE_PIXELS,seed);mode(false);ht_upscale_scene();memcpy(want,ht_scene,HT_PIXELS);
  mode(true);ht_upscale_scene();assert(!memcmp(want,ht_scene,HT_PIXELS));
  pattern(ht_recon_scene,HT_RECON_PIXELS,seed);mode(false);ht_reconstruct_low_scene();memcpy(want,ht_low_scene,HT_SCENE_PIXELS);
  mode(true);ht_reconstruct_low_scene();assert(!memcmp(want,ht_low_scene,HT_SCENE_PIXELS));
 }
 /* All byte offsets through a cache seam, including negative world keys. */
 for(int d=0;d<HT_LAYERS;++d)for(int slot=0;slot<HT_TILE_SLOTS;++slot){
  pattern(ht_cached_near[d][slot],HT_TILE_PIXELS,7+d*2+slot);
  pattern(ht_cached_wide[d][slot],HT_TILE_PIXELS,17+d+slot*2);
 }
 for(int offset=-273;offset<=273;++offset)for(int depth=0;depth<HT_LAYERS;++depth){
  pattern(ht_recon_scene,HT_RECON_PIXELS,3);mode(false);ht_composite_cached_ai(depth,offset,offset%480,offset%270);
  memcpy(want,ht_recon_scene,HT_RECON_PIXELS);
  pattern(ht_recon_scene,HT_RECON_PIXELS,3);mode(true);ht_composite_cached_ai(depth,offset,offset%480,offset%270);
  assert(!memcmp(want,ht_recon_scene,HT_RECON_PIXELS));
 }
 pattern(ht_raw,HT_PIXELS,31);
 for(int radius=0;radius<=10;++radius)for(int offset=0;offset<16;++offset){
  int x0=offset,x1=HT_W-offset,y0=offset%3,y1=HT_H-offset%5;
  memset(want,0xa5,HT_PIXELS);memset(actual,0xa5,HT_PIXELS);
  mode(false);ht_blur_rect(ht_raw,want,radius,x0,x1,y0,y1);
  mode(true);ht_blur_rect(ht_raw,actual,radius,x0,x1,y0,y1);
  assert(!memcmp(want,actual,HT_PIXELS));
  /* A one-row band uses valid but equal add/sub pointers on its last row. */
  mode(false);ht_blur_rect(ht_raw,want,radius,x0,x1,y0,y0+1);
  mode(true);ht_blur_rect(ht_raw,actual,radius,x0,x1,y0,y0+1);
  assert(!memcmp(want,actual,HT_PIXELS));
 }
 /* Fresh cache build in each mode, not just a warmed cache produced by AI. */
 for(unsigned level=0;level<HT_LEVELS;++level)for(int view=0;view<3;++view){
  ht_bind(mem);ht_service=service;ht.level=level;ht_spawn(true);
  ht.camera=view*733*256;ht.x=(view*733+190)*256;ht.vista=view?256:0;
  ht.sway_phase=512;ht.rotation_phase=256u<<8;ht_game game=ht;
  mode(false);ht_render_scene();memcpy(want,ht_scene,HT_PIXELS);
  ht_bind(mem);ht_service=service;ht=game;mode(true);ht_render_scene();
  assert(!memcmp(want,ht_scene,HT_PIXELS));
 }
 /* A rejected device self-test must not select any new arithmetic. */
 ht_expanded_ready=false;ht_simd_stage_ready=0;assert(!ht_expanded_active(HT_OPT_SIMD_ALL));
 ht_render_scene();memcpy(want,ht_scene,HT_PIXELS);ht_render_scene();
 assert(!memcmp(want,ht_scene,HT_PIXELS));assert(checks>0);
 free(actual);free(want);free(mem);
 puts("Expanded SIMD: arithmetic, row edges, cache seams, blur bands, fresh frames in all ten chapters and fallback PASS (host oracles; device self-test verifies opcodes)");
}
