/* Reviewed forest depth/crossing frames; the other 27 chapter captures
 * preserve the restored scenery and nearest-camera renderer. */
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
{3282416970,1961819723},
{1881086879,95639244},
{1247327868,597011144},
{1206504672,426343886},
{682691727,1565290177},
{2572221211,4267103357},
{4041132480,2400766959},
{587258285,9943633},
{2586757509,3333758860},
{457027355,1816846650},
{1966292873,4146631744},
{2539700118,1390069209},
{573902002,2490546487},
{3064061895,3391950038},
{2016817927,2302120057},
{1801411052,3950480477},
{3921345165,2007190506},
{3859415399,70798080},
{2717882469,2323644206},
{3499433723,792856704},
{1737739056,2168347357},
{2238371299,2993415012},
{3344137295,259530570},
{2882779981,2519302173},
{2576064911,3369189100},
{4152371199,1509609613},
{3134402464,1514944594},
{1896499223,1053304696},
{2053250862,892301618},
{891720619,2224482618},
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
 /* Independent rounded-coordinate oracle for nearest camera sampling.
  * Cover both rotation directions, mood and drop zoom, and clipped borders. */
 for(unsigned i=0;i<HT_PIXELS;++i)ht_temp[i]=(uint8_t)ht_hash(i);
 for(unsigned n=0;n<96;++n){
  ht_game g={0};g.sway_phase=n*17;g.rotation_phase=n*73*256;
  g.camera_mood=n%3==0?256:n%257;g.drop_zoom=n%4==0?256:n%257;
  int turn,scale;ht_camera_coefficients(&g,&turn,&scale);
  memset(ht_scene,0x5a,HT_PIXELS);ht_camera_into(ht_temp,&g,HT_CAMERA_BASELINE);
  for(int y=0;y<HT_H;++y)for(int x=0;x<HT_W;++x){
   if(y<HT_BORDER||y>=HT_H-HT_BORDER||x<ht_visible_left[y]||x>=HT_W-ht_visible_left[y]){
    assert(ht_scene[y*HT_W+x]==0x5a);continue;
   }
   int u=(HT_W/2)*4096+(x-HT_W/2)*scale+(y-HT_H/2)*turn;
   int v=(HT_H/2)*4096-(x-HT_W/2)*turn+(y-HT_H/2)*scale;
   int sx=(u+2048)>>12,sy=(v+2048)>>12;
   assert(sx>=0&&sx<HT_W&&sy>=0&&sy<HT_H);
   assert(ht_scene[y*HT_W+x]==ht_temp[sy*HT_W+sx]);
  }
 }
 /* Zoom-only must exactly match a zero-rotation affine reference; pure
  * rotation must stay in bounds without secretly retaining a zoom crop. */
 for(unsigned n=0;n<96;++n){
  ht_game g={0};g.sway_phase=n*17;g.rotation_phase=n*73*256;
  g.camera_mood=256;g.drop_zoom=n%257;
  int scale=4096-(ht_sway_wave(g.sway_phase/2+768)+256)*4/3;
  scale+=(4096-scale)*g.drop_zoom/256;
  memset(ht_scene,0x5a,HT_PIXELS);ht_affine_into(ht_temp,0,scale);
  memcpy(ht_raw,ht_scene,HT_PIXELS);
  memset(ht_scene,0x5a,HT_PIXELS);ht_camera_into(ht_temp,&g,HT_CAMERA_NO_ROTATION);
  assert(!memcmp(ht_raw,ht_scene,HT_PIXELS));
  int turn,unused;ht_camera_coefficients(&g,&turn,&unused);
  ht_camera_into(ht_temp,&g,HT_CAMERA_NO_ZOOM);
  for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)
   for(int x=ht_visible_left[y];x<HT_W-ht_visible_left[y];++x){
    int sx=ht_clamp((x*4096+(y-HT_H/2)*turn+2048)>>12,0,HT_W-1);
    int sy=ht_clamp((y*4096-(x-HT_W/2)*turn+2048)>>12,0,HT_H-1);
    assert(ht_scene[y*HT_W+x]==ht_temp[sy*HT_W+sx]);
   }
 }
 /* Both-off ignores camera animation and vista while keeping live state. */
 ht_camera_mode=HT_CAMERA_OFF;ht.level=0;ht_spawn(true);
 ht.sway_phase=128;ht.rotation_phase=512*256;ht.vista=256;ht_render_scene();
 memcpy(ht_raw,ht_scene,HT_PIXELS);
 assert(ht.vista==256);
 ht.sway_phase=0;ht.rotation_phase=0;ht.vista=0;ht_render_scene();
 assert(!memcmp(ht_raw,ht_scene,HT_PIXELS));
 ht_camera_mode=HT_CAMERA_BASELINE;
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
 free(bits);free(mem);puts("Production renderer: mode24+13+nearest golden frames/packing, all readiness masks, blur boundaries and FPS window PASS");
}
