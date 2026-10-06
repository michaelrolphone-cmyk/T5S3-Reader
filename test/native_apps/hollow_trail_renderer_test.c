/* Reviewed 1.1.31 native-resolution A/B renderer. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "../../Apps/hollow_trail_fps.inc"
static unsigned full_height_checkpoints,half_height_checkpoints;
static void observe_production_raster(void) {
 if(!ht_native_active)return;
 if(ht_native_foreground_half_y)++half_height_checkpoints;
 else ++full_height_checkpoints;
}
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
static void cab_step_plant_tests(uint8_t *mem,uint8_t *bits){
 /* Novella IV: three leaves on the locomotive's existing iron cab tread.
  * Check each separate leaf tip in the actual geography pass, including the
 * half-height native foreground. These regions are bare on the old source. */
 static const int leaves[3][4]={{132,-27,137,-22},{144,-30,149,-25},{138,-34,143,-29}};
 uint8_t *without_leaves=malloc(HT_NATIVE_PIXELS/8);assert(without_leaves);
 for(unsigned mode=0;mode<3;++mode)for(int scale=256;scale<=384;scale+=128){
  ht_bind(mem);ht_bind_native(mem);memset(&ht,0,sizeof(ht));ht.level=3;ht_spawn(true);
  ht.camera=560*256;ht.camera_y=40*256;ht_world_scale=scale;
  ht_native_active=mode!=0;ht_native_foreground_half_y=mode==2;
  ht_scene=mode?ht_native_a:ht_scene_low;
  int raster=mode?2:1,width=HT_W*raster;
  size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
  memset(ht_scene,0,bytes);ht_game retained=ht;ht_scene_geography(&ht);
  assert(!memcmp(&ht,&retained,sizeof(ht)));
  for(unsigned leaf=0;leaf<3;++leaf){
   int left=ht_project_x(640-560+leaves[leaf][0])*raster;
   int right=ht_project_x(640-560+leaves[leaf][2])*raster;
   int top=ht_project_y(208-40+leaves[leaf][1])*raster;
   int bottom=ht_project_y(208-40+leaves[leaf][3])*raster;
   unsigned pixels=0;
   for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x)
    pixels+=ht_scene[y*width+x]==96;
   assert(pixels>0);
  }
  /* The stem is grounded directly on the existing upper tread. */
  int root_x=ht_project_x(640-560+139)*raster,root_y=ht_project_y(208-40-16)*raster;
  assert(ht_scene[root_y*width+root_x]==116);
  ht_native_foreground_half_y=false;
  ht_pack_mono(bits,HT_NATIVE_W/8);
  /* Replace only leaf fill with iron tone. Each separate tip must make a
   * visible difference after the production 1-bit packer, not just in gray. */
  for(size_t i=0;i<bytes;++i)if(ht_scene[i]==96)ht_scene[i]=175;
  ht_pack_mono(without_leaves,HT_NATIVE_W/8);
  for(unsigned leaf=0;leaf<3;++leaf){
   int left=ht_project_x(640-560+leaves[leaf][0])*2;
   int right=ht_project_x(640-560+leaves[leaf][2])*2;
   int top=ht_project_y(208-40+leaves[leaf][1])*2;
   int bottom=ht_project_y(208-40+leaves[leaf][3])*2;
   unsigned changed=0;
   for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){
    int at=y*(HT_NATIVE_W/8)+x/8;
    changed+=((bits[at]^without_leaves[at])&(0x80u>>(x&7)))!=0;
   }
   assert(changed>0);
  }
 }
 free(without_leaves);
 ht_bind(mem);ht_bind_native(mem);
}
static void native_resolution_tests(uint8_t *mem,uint8_t *bits){
 static const uint8_t rank[8][8]={
  {41,8,52,7,45,15,47,4},{18,35,27,60,28,55,19,57},
  {54,1,49,9,62,3,40,14},{16,56,21,53,20,38,25,43},
  {59,12,42,6,39,10,34,5},{24,33,17,46,31,50,29,61},
  {36,2,58,11,63,0,51,13},{26,44,30,37,22,32,23,48}
 };
 ht_bind(mem);ht_bind_native(mem);
 assert(ht_native_a+HT_NATIVE_PIXELS==ht_native_b);
 assert(ht_native_a==mem+HT_MEMORY);
 assert(ht_native_b+HT_NATIVE_PIXELS==mem+HT_MEMORY+HT_NATIVE_MEMORY);
 /* A diagonal procedural primitive must use physical subpixels instead of
  * becoming four identical panel pixels per 480x270 logical sample. */
 memset(ht_native_a,0,HT_NATIVE_PIXELS);
 ht_scene=ht_native_a;ht_native_active=true;ht_framed=false;
 ht_triangle(ht_scene,21,19,83,57,34,111,255);
 bool mixed=false;
 for(int y=0;y<HT_H && !mixed;++y)for(int x=0;x<HT_W && !mixed;++x){
  int at=2*y*HT_NATIVE_W+2*x;
  uint8_t a=ht_native_a[at],b=ht_native_a[at+1],c=ht_native_a[at+HT_NATIVE_W],d=ht_native_a[at+HT_NATIVE_W+1];
  mixed=(a!=b)||(a!=c)||(a!=d);
 }
 assert(mixed);
 /* The halfway foreground keeps native X but computes only 270 Y rows.
  * Every foreground scanline must be copied to its physical row partner,
  * while horizontal edges can still land on odd panel columns. */
 memset(ht_native_a,0,HT_NATIVE_PIXELS);
 ht_scene=ht_native_a;ht_native_active=true;ht_native_foreground_half_y=true;
 ht_triangle(ht_scene,21,19,83,57,34,111,255);
 bool odd_x=false;
 for(int y=0;y<HT_H;++y) {
  assert(!memcmp(ht_native_a+(2*y)*HT_NATIVE_W,
                 ht_native_a+(2*y+1)*HT_NATIVE_W,HT_NATIVE_W));
  for(int x=0;x<HT_W-1 && !odd_x;++x) {
   int at=2*y*HT_NATIVE_W+2*x;
   odd_x=ht_native_a[at]!=ht_native_a[at+1];
  }
 }
 assert(odd_x);
 ht_native_foreground_half_y=false;
 /* Native packing is one grayscale sample per physical dot. */
 for(unsigned i=0;i<HT_NATIVE_PIXELS;++i)ht_native_a[i]=(uint8_t)hash((const uint8_t *)&i,sizeof(i));
 ht_pack_mono(bits,HT_NATIVE_W/8);
 for(int y=0;y<HT_NATIVE_H;++y)for(int byte=0;byte<HT_NATIVE_W/8;++byte){
  uint8_t want=0;int x=byte*8;
  for(int k=0;k<8;++k)if(ht_native_a[y*HT_NATIVE_W+x+k]>(int)rank[y&7][(x+k)&7]*4+2)want|=(uint8_t)(0x80u>>k);
  assert(bits[y*(HT_NATIVE_W/8)+byte]==want);
 }
 /* Exercise the complete native mode through the normal render/camera path
  * and require deterministic physical output for a frozen game snapshot. */
 ht_bind(mem);ht_bind_native(mem);ht_camera_mode=HT_CAMERA_NATIVE;ht.level=0;ht_spawn(true);
 ht.camera=733*256;ht.x=(733+190)*256;ht.vista=256;ht.sway_phase=445;
 ht.rotation_phase=347u<<8;ht.camera_mood=256;
 ht_service=observe_production_raster;
 ht_render_scene();assert(ht_native_active && !ht_native_foreground_half_y && ht_scene==ht_native_a);
 assert(full_height_checkpoints && half_height_checkpoints);ht_service=NULL;
 ht_pack_mono(bits,HT_NATIVE_W/8);unsigned first=hash(bits,HT_NATIVE_PIXELS/8);
 ht_render_scene();ht_pack_mono(bits,HT_NATIVE_W/8);
 assert(first==hash(bits,HT_NATIVE_PIXELS/8));
 ht_camera_mode=HT_CAMERA_BASELINE;ht_render_scene();
 assert(!ht_native_active && ht_scene==ht_scene_low);
}

static const unsigned golden[][2]={
{3305791424,2424592664}, /* Reviewed marked tree and grounded sawn roots; other 29 views unchanged. */
{694281688,980295134}, /* Reviewed physical shutter/sill and interior floor; other 29 pairs unchanged. */
{2685907447,1969813067},
{816840540,30581379},
{3011208540,878481330},
{1203433882,3648215422},
{2620597804,2036500297},
{1863941504,1514272394},
{3644739766,32595887},
{1846896691u,846753261u},
{3241840614,1097779836},
{223795782u,2437891813u}, /* Reviewed lower trestle/ditch; corrected-map cabin and other 29 pairs retained. */
{2033607303,4272025260},
{737287855,150225469},
{4025276642u,2003882676u}, /* Reviewed raised bank/cloud shallows and quarry; other 29 pairs exact. */
{1684780377,2737385310},
{1477351146,501319504},
{3095071177,861699871},
{3195525885u,56340071u}, /* Reviewed shared gallery, pane and three ridge-anchored rows. */
{1623275131u,2992220726u}, /* Same three rows recede with the existing camera. */
{2250815207u,1628315133u}, /* Rows leave the horizon; grounded count-row care is retained. */
{1305748946u,1440843516u}, /* Turbine approach rail and puddle on support zero; other 29 pairs retained. */
{3747272453,2052152225},
{3385086579u,435798566u}, /* Far valve furniture on unchanged support four; other 29 pairs retained. */
{413184930,1205470550},
{1805041436,3386618820},
{3469364471,1119496009},
{4124658611,3847112319},
{297423422,3292122172},
{3021503600,3314516329},
};
int main(void){
 fps_tests();
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_PIXELS/2);assert(mem&&bits);
 cab_step_plant_tests(mem,bits);
 for(unsigned ready=0;ready<=HT_OPT_SIMD_ALL;++ready)
  for(unsigned level=0;level<HT_LEVELS;++level)for(int view=0;view<3;++view){
   ht_bind(mem);ht.level=level;ht_spawn(true);ht.traversal.boat_x=ht_mech(&ht)->boat_left*256; /* Frozen pre-retrieval renderer fixture. */
   ht.camera=view*733*256;ht.x=(view*733+190)*256;
   ht.vista=view?256:0;ht.sway_phase=view?128+view*317:0;ht.rotation_phase=(128+view*219)<<8;ht.camera_mood=256;
   ht_simd_stage_ready=ready;ht_simd_ready=ready!=0;
   ht_render_scene();ht_pack_mono(bits,HT_W/4);
   assert(hash(ht_scene,HT_PIXELS)==golden[level*3+view][0]);
   assert(hash(bits,HT_PIXELS/2)==golden[level*3+view][1]);
  }
 native_resolution_tests(mem,bits);
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
 free(bits);free(mem);puts("Production renderer: baseline goldens plus full-native backgrounds, 960x270 foreground detail, packing, camera, blur and FPS contracts PASS");
}
