/* Settlement scenery is visible, supported and immutable in all raster paths. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned hash(const uint8_t *p,size_t n){unsigned h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static void draw(const ht_game *g){ht_settlement_streets(g);ht_first_house_room(g,0);ht_settlement_returns(g);}
static void fixture(uint8_t *memory,int mode,int scale,int camera){
 ht_bind(memory);ht_bind_native(memory);memset(&ht,0,sizeof(ht));ht.level=9;ht_select_level(9);ht_spawn(true);
 ht.camera=camera*256;ht.camera_y=0;ht_world_scale=scale;ht_framed=false;
 ht_native_active=mode!=0;ht_native_foreground_half_y=mode==2;
 ht_scene=mode?ht_native_a:ht_scene_low;
}
static unsigned region_difference(const uint8_t *a,const uint8_t *b,int left,int top,int right,int bottom){
 unsigned changed=0;
 for(int y=ht_max(0,ht_project_y(top)*2);y<=ht_min(HT_NATIVE_H-1,ht_project_y(bottom)*2);++y)
  for(int x=ht_max(0,ht_project_x(left)*2);x<=ht_min(HT_NATIVE_W-1,ht_project_x(right)*2);++x){
   int at=y*(HT_NATIVE_W/8)+x/8;changed+=((a[at]^b[at])&(0x80u>>(x&7)))!=0;
  }
 return changed;
}
int main(void){
 uint8_t *memory=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*bits=malloc(HT_NATIVE_PIXELS/8),*empty=malloc(HT_NATIVE_PIXELS/8);
 assert(memory && bits && empty);
 /* World coordinates are on the original supports; the two street gaps stay
  * exposed. Furniture is scenery, so the crate and every ladder remain real. */
 fixture(memory,0,256,0);
 assert(ht_surface_at(&ht,0,HT_FIRST_HOUSE_X)==220);
 assert(ht_surface_at(&ht,1,HT_SETTLEMENT_BOWL_X)==180);
 assert(ht_surface_at(&ht,1,HT_SETTLEMENT_SCRAPER_X)==180);
 assert(ht_surface_at(&ht,2,HT_SETTLEMENT_CISTERN_X)==180);
 assert(ht_level_land(9)[0].right==430 && ht_level_land(9)[1].left==454);
 assert(ht_level_land(9)[1].right==760 && ht_level_land(9)[2].left==770);
 assert(ht_mech(&ht)->crate_x==530 && ht_mech(&ht)->ladders[0].x==1020);
 /* Check each ordinary trace after the production monochrome packer, rather
  * than depending on a screenshot caption or a grayscale-only pixel. */
 static const int regions[][6]={
  {430,581,173,599,180,91}, /* inverted bowl shell */
  {430,586,167,595,173,159}, /* separate stone weighting its foot */
  {430,695,171,711,180,227}, /* cleaned iron blade/feet */
  {430,714,173,728,180,194}, /* separate shaped mud heap */
  {430,598,124,605,135,189}, /* one of seven clipped clothespins */
  {700,807,120,847,166,218}, /* dead vine and bowed trellis */
  {700,867,165,921,178,231}, /* freshly turned bed */
  {700,949,148,990,180,123}, /* cistern rim/body on support two */
  {120,281,178,330,184,229}, /* return reaches first house's actual pane */
  {430,620,124,648,178,229}, /* receding alley with opaque wall corners */
  {2690,2818,68,2888,75,229} /* inward feed at the original lamp terrace */
 };
 for(int mode=0;mode<3;++mode)for(int scale=256;scale<=384;scale+=128){
  for(unsigned n=0;n<sizeof(regions)/sizeof(regions[0]);++n){
   int camera=regions[n][0];fixture(memory,mode,scale,camera);
   size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;memset(ht_scene,30,bytes);
   ht_native_foreground_half_y=false;ht_pack_mono(empty,HT_NATIVE_W/8);ht_native_foreground_half_y=mode==2;
   ht_game frozen=ht;draw(&frozen);assert(!memcmp(&ht,&frozen,sizeof(ht)));
   unsigned first=hash(ht_scene,bytes);
   if(mode==2)for(int y=0;y<HT_H;++y)assert(!memcmp(ht_scene+y*2*HT_NATIVE_W,ht_scene+(y*2+1)*HT_NATIVE_W,HT_NATIVE_W));
   ht_native_foreground_half_y=false;ht_pack_mono(bits,HT_NATIVE_W/8);
   assert(region_difference(bits,empty,regions[n][1]-camera,regions[n][2],regions[n][3]-camera,regions[n][4])>3);
   /* Remove only this trace's visible material inside its surveyed bounds.
    * The surrounding doorway/wall cannot satisfy the visibility check. */
   int factor=mode?2:1,width=HT_W*factor,height=HT_H*factor;unsigned material=0;
   for(int y=ht_max(0,ht_project_y(regions[n][2])*factor);y<=ht_min(height-1,ht_project_y(regions[n][4])*factor);++y)
    for(int x=ht_max(0,ht_project_x(regions[n][1]-camera)*factor);x<=ht_min(width-1,ht_project_x(regions[n][3]-camera)*factor);++x)
     if(ht_scene[y*width+x]==regions[n][5]){ht_scene[y*width+x]=30;++material;}
   if(!material)fprintf(stderr,"missing trace material region=%u mode=%d scale=%d\n",n,mode,scale);
   assert(material>0);ht_pack_mono(empty,HT_NATIVE_W/8);
   unsigned visible=region_difference(bits,empty,regions[n][1]-camera,regions[n][2],regions[n][3]-camera,regions[n][4]);
   if(!visible)fprintf(stderr,"invisible trace region=%u mode=%d scale=%d material=%u\n",n,mode,scale,material);
   assert(visible>0);
   ht_native_foreground_half_y=mode==2;memset(ht_scene,30,bytes);draw(&frozen);assert(first==hash(ht_scene,bytes));
   frozen.ticks+=500;memset(ht_scene,30,bytes);draw(&frozen);assert(first==hash(ht_scene,bytes));
  }
  fixture(memory,mode,256,350);size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
  memset(ht_scene,30,bytes);draw(&ht);int factor=mode?2:1;
  for(int x=430;x<454;++x)assert(ht_scene[219*factor*HT_W*factor+(x-350)*factor]==30);
  fixture(memory,mode,256,600);memset(ht_scene,30,bytes);draw(&ht);
  for(int x=760;x<770;++x)assert(ht_scene[179*factor*HT_W*factor+(x-600)*factor]==30);
  for(int gate=0;gate<2;++gate){
   fixture(memory,mode,scale,430);memset(ht_scene,30,bytes);unsigned clear=hash(ht_scene,bytes);
   if(gate)ht.door_stage=HT_DOOR_YARD;else ht.level=8;
   draw(&ht);assert(clear==hash(ht_scene,bytes));
  }
 }
 /* The complete renderer uses the scenery, with frozen snapshots in both
  * baseline and native camera modes. Existing actor/control state stays intact. */
 for(int native=0;native<2;++native)for(int view=0;view<4;++view){
  static const int cameras[]={70,430,710,2690};fixture(memory,0,256,cameras[view]);
  ht_camera_mode=native?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht.x=(cameras[view]+180)*256;
  ht_game frozen=ht;ht_render_scene_from(&frozen,true);ht_pack_mono(bits,HT_NATIVE_W/8);
  unsigned first=hash(bits,HT_NATIVE_PIXELS/8);ht_render_scene_from(&frozen,true);ht_pack_mono(bits,HT_NATIVE_W/8);
  assert(first==hash(bits,HT_NATIVE_PIXELS/8) && !memcmp(&ht,&frozen,sizeof(ht)));
 }
 free(empty);free(bits);free(memory);
 puts("Settlement: grounded ordinary traces, packed-mono visibility, unchanged gaps, inward window/terrace returns, frozen baseline/native/half-height and chapter guards PASS");
 return 0;
}
