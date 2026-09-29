#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "../../Apps/hollow_trail_fps.inc"
static void fps_tests(void){
 ht_fps_window w;ht_fps_reset(&w,0);
 for(unsigned t=100;t<=10000;t+=100)assert(ht_fps_push(&w,t)==100);
 assert(w.elapsed==10000 && w.count==100);
 /* Five more seconds at 20 FPS leaves five seconds each of 10 and 20 FPS. */
 for(unsigned t=10050;t<=15000;t+=50)ht_fps_push(&w,t);
 assert(w.count==150 && ht_fps_push(&w,16000)==141); /* 140 prior + new frame */
 ht_fps_reset(&w,16000);assert(w.count==0 && w.elapsed==0);
 assert(ht_fps_push(&w,16200)==50 && w.elapsed==200);
 /* Pause/resume restarts sampling; old timestamps cannot lower resumed FPS. */
 ht_fps_reset(&w,60000);assert(ht_fps_push(&w,60100)==100);
 ht_fps_reset(&w,UINT32_MAX-5000u);
 for(unsigned age=42;age<=20000;age+=42)ht_fps_push(&w,(UINT32_MAX-5000u)+age);
 assert(w.elapsed==10000 && w.count>=238 && w.count<=239);
}
int main(void){
 fps_tests();
 for(int distance=-10000;distance<=1000000;++distance)
  assert(ht_focus_bounded(distance)==(unsigned)ht_clamp((distance-2500)/200,0,256));
 ht_nn_cache_reset();assert(sizeof(ht_nn_cache)==4096);
 for(unsigned n=0;n<30000;++n){
  uint8_t patch[HT_NN_INPUTS];int16_t expected[HT_NN_OUTPUTS],actual[HT_NN_OUTPUTS];
  for(unsigned i=0;i<HT_NN_INPUTS;++i)patch[i]=(uint8_t)ht_hash(n*13+i);
  ht_nn_residual(patch,expected);
  ht_nn_residual_cached(patch,actual);assert(!memcmp(expected,actual,sizeof(actual)));
  uint32_t hits=ht_nn_cache_hits;
  ht_nn_residual_cached(patch,actual);assert(!memcmp(expected,actual,sizeof(actual)) && ht_nn_cache_hits==hits+1);
 }
 ht_nn_cache_reset();assert(!ht_nn_cache_hits && !ht_nn_cache_lookups);
 uint32_t scene_hits=0,scene_queries=0;
 uint8_t *mem=malloc(HT_MEMORY),*want=malloc(HT_PIXELS);
 uint8_t *fast=malloc(HT_FAST_ALLOC_BYTES);
 assert(mem&&want&&fast);ht_bind(mem);ht_workspace_attach(fast);
 for(unsigned i=0;i<HT_TEST_COUNT;++i){
  ht_render_test=i;unsigned mask=ht_render_test_mask();
  if(i==HT_TEST_BASE)assert(mask==0);
  else if(i==HT_TEST_ALL_FOUR)assert(mask==HT_OPT_SIMD_ALL);
  else assert(mask && !(mask&(mask-1)));
  for(unsigned ready=0;ready<16;++ready){
   ht_simd_stage_ready=ready;
   assert(ht_render_test_available()==((mask&15u&ready)==(mask&15u)));
   for(unsigned bit=1;bit<=8;bit*=2)assert(ht_expanded_active(bit)==!!(mask&ready&bit));
  }
 }
 ht_render_test=HT_TEST_BASE;for(unsigned i=0;i<HT_TEST_COUNT;++i)ht_render_test_next();assert(ht_render_test==HT_TEST_BASE);
 for(unsigned n=0;n<=65152;++n)assert(ht_div255(n)==n/255);
 for(unsigned n=0;n<20000;++n){
  uint32_t h=ht_hash(n);uint8_t a[2]={(uint8_t)h,(uint8_t)(h>>8)},b[2]={(uint8_t)(h>>16),(uint8_t)(h>>24)};
  for(unsigned fx=0;fx<16;++fx)for(unsigned fy=0;fy<16;++fy){
   int top=a[0]*16+((int)a[1]-a[0])*(int)fx,bottom=b[0]*16+((int)b[1]-b[0])*(int)fx;
   assert(ht_camera_pixel_packed(a,b,fx,fy)==((top*16+(bottom-top)*(int)fy)>>8));
  }
 }
 for(unsigned n=0;n<3000;++n){
  int xy[6];for(unsigned j=0;j<6;++j)xy[j]=(int)(ht_hash(n*7+j)%1200)-400;
  if(n%5==0)xy[3]=xy[1];
  if(n%7==0)xy[5]=xy[1];
  ht_world_scale=n&1?176:256;
  memset(ht_scene,0,HT_PIXELS);ht_render_test=HT_TEST_BASE;ht_triangle(ht_scene,xy[0],xy[1],xy[2],xy[3],xy[4],xy[5],135);memcpy(want,ht_scene,HT_PIXELS);
  memset(ht_scene,0,HT_PIXELS);ht_render_test=HT_TEST_TRIANGLE;ht_triangle(ht_scene,xy[0],xy[1],xy[2],xy[3],xy[4],xy[5],135);
  assert(!memcmp(want,ht_scene,HT_PIXELS));
 }
 ht_world_scale=256;
 /* Random pixels, edge extension, one-pixel and full-width blur rectangles. */
 for(unsigned i=0;i<HT_PIXELS;++i)ht_raw[i]=(uint8_t)ht_hash(i);
 for(unsigned n=0;n<600;++n){
  int radius=1+n%9,x0=ht_hash(n)%HT_W,x1=x0+1+ht_hash(n+731)%(HT_W-x0);
  if(n%3==0)x0=0;
  if(n%5==0)x1=HT_W;
  if(n%7==0)x1=x0+1;
  int y0=ht_hash(n+891)%HT_H,y1=ht_min(HT_H,y0+1+(int)(n%17));
  uint32_t reciprocal=((1u<<24)+(2*radius+1)-1)/(2*radius+1);
  memset(ht_filter,0xa5,HT_PIXELS);ht_render_test=HT_TEST_BASE;
  ht_blur_horizontal(ht_raw,radius,reciprocal,x0,x1,y0,y1);memcpy(want,ht_filter,HT_PIXELS);
  memset(ht_filter,0xa5,HT_PIXELS);ht_render_test=HT_TEST_BLUR_INTERIOR;
  ht_blur_horizontal(ht_raw,radius,reciprocal,x0,x1,y0,y1);assert(!memcmp(want,ht_filter,HT_PIXELS));
 }
 /* Every byte alignment and tile transition, including negative world keys.
  * Randomized cache pixels and initial scene also exercise max/occlusion. */
 for(int d=0;d<HT_LAYERS;++d)for(int slot=0;slot<HT_TILE_SLOTS;++slot)
  for(unsigned i=0;i<HT_TILE_PIXELS;++i){
   ht_cached_near[d][slot][i]=(uint8_t)ht_hash(i+slot*731+d*71);
   ht_cached_wide[d][slot][i]=(uint8_t)ht_hash(i+slot*193+d*839);
  }
 for(int offset=-1024;offset<=1024;++offset){
  int depth=(offset+1024)%HT_LAYERS,fx=offset%HT_W,fy=offset%HT_H;
  for(unsigned i=0;i<HT_RECON_PIXELS;++i)ht_recon_scene[i]=(uint8_t)ht_hash(i);
  ht_render_test=HT_TEST_BASE;ht_composite_cached_ai(depth,offset,fx,fy);
  memcpy(want,ht_recon_scene,HT_RECON_PIXELS);
  for(unsigned i=0;i<HT_RECON_PIXELS;++i)ht_recon_scene[i]=(uint8_t)ht_hash(i);
  ht_render_test=HT_TEST_TILE_PLAN;ht_composite_cached_ai(depth,offset,fx,fy);
  assert(!memcmp(want,ht_recon_scene,HT_RECON_PIXELS));
 }
 for(unsigned level=0;level<HT_LEVELS;++level)for(int view=0;view<3;++view){
  ht_bind(mem);ht.level=level;ht_spawn(true);ht.camera=view*733*256;ht.x=(view*733+190)*256;
  ht.vista=view?256:0;ht.sway_phase=128+view*317;ht.rotation_phase=(128+view*219)<<8;ht.camera_mood=256;ht_game game=ht;
  ht_render_scene();memcpy(want,ht_scene,HT_PIXELS);
  for(unsigned mode=1;mode<HT_TEST_COUNT;++mode){
   if(mode==HT_TEST_LOW_CAMERA)continue;
   ht_bind(mem);ht_workspace_attach(fast);ht=game;ht_render_test=mode;ht_simd_stage_ready=HT_OPT_SIMD_ALL;
   ht_render_scene();assert(!memcmp(want,ht_scene,HT_PIXELS));
   if(mode==HT_TEST_NN_CACHE){scene_hits+=ht_nn_cache_hits;scene_queries+=ht_nn_cache_lookups;}
  }
 }
 printf("Neural cache scene workload: %u/%u exact hits (reuse count, not an S3 timing result)\n",scene_hits,scene_queries);
 ht_workspace_attach(NULL);free(fast);free(want);free(mem);puts("Render tests: twenty modes (nineteen exact), exact math, all chapters and rolling 10-second FPS PASS");
}
