/* Golden frames captured from 1.1.19 mode 24 + mode 13 before cleanup. */
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

static unsigned hash(const uint8_t *p,int n){unsigned h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static const unsigned golden[][2]={
{2732453282,437489224},
{3260895424,3051058036},
{4178722851,170864257},
{1206504672,426343886},
{3212196771,1483222129},
{3811253152,1703987057},
{4041132480,2400766959},
{904422213,2245926133},
{1539769896,953887075},
{457027355,1816846650},
{1894273545,1514651272},
{1383794233,2999192548},
{573902002,2490546487},
{4057979974,2047409066},
{2610287337,52893428},
{1801411052,3950480477},
{3913139421,1369944594},
{737427685,2835885594},
{2717882469,2323644206},
{2059172262,2938124473},
{2196817201,3289471923},
{2238371299,2993415012},
{309439037,3966249920},
{3497430071,3218515412},
{2576064911,3369189100},
{268429169,2194444544},
{770949756,1608480135},
{1896499223,1053304696},
{3191972242,2970895352},
{1522090091,2657649712},
};
int main(void){
 fps_tests();
 uint8_t *mem=malloc(HT_MEMORY),*bits=malloc(HT_PIXELS/2);assert(mem&&bits);
 for(unsigned ready=0;ready<=HT_OPT_SIMD_ALL;++ready)
  for(unsigned level=0;level<HT_LEVELS;++level)for(int view=0;view<3;++view){
   ht_bind(mem);ht.level=level;ht_spawn(true);ht.camera=view*733*256;ht.x=(view*733+190)*256;
   ht.vista=view?256:0;ht.sway_phase=view?128+view*317:0;ht.rotation_phase=(128+view*219)<<8;ht.camera_mood=256;
   ht_simd_stage_ready=ready;ht_simd_ready=ready!=0;
   ht_render_scene();ht_pack_mono(bits,HT_W/4);
   assert(hash(ht_scene,HT_PIXELS)==golden[level*3+view][0]);
   assert(hash(bits,HT_PIXELS/2)==golden[level*3+view][1]);
  }
 /* Independent clamped horizontal-blur oracle: boundaries, narrow ranges,
  * and random source pixels preserve the original running-sum arithmetic. */
 for(unsigned i=0;i<HT_PIXELS;++i)ht_raw[i]=(uint8_t)ht_hash(i);
 for(unsigned n=0;n<600;++n){
  int radius=1+n%9,x0=ht_hash(n)%HT_W,x1=x0+1+ht_hash(n+731)%(HT_W-x0);
  if(n%3==0)x0=0;
  if(n%5==0)x1=HT_W;
  if(n%7==0)x1=x0+1;
  int y=n%HT_H;uint32_t reciprocal=((1u<<24)+2*radius)/(2*radius+1);
  memset(ht_filter,0x5a,HT_PIXELS*sizeof(*ht_filter));
  ht_blur_horizontal(ht_raw,radius,reciprocal,x0,x1,y,y+1);
  for(int x=0;x<HT_W;++x){
   if(x<x0 || x>=x1){assert(ht_filter[y*HT_W+x]==0x5a5a);continue;}
   int sum=0;for(int k=-radius;k<=radius;++k)sum+=ht_raw[y*HT_W+ht_clamp(x+k,0,HT_W-1)];
   assert(ht_filter[y*HT_W+x]==ht_average(sum,reciprocal));
  }
 }
 free(bits);free(mem);puts("Production renderer: mode24+13 golden frames/packing, all readiness masks, blur boundaries and FPS window PASS");
}
