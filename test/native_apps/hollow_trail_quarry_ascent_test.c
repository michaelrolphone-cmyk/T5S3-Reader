/* Quarry traces and backward country remain a view over the actual route. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
#include "hollow_trail_route_walk.inc"
static unsigned services;
static void service(void){++services;}
static unsigned pixels_changed(const uint8_t *a,const uint8_t *b,int l,int t,int r,int bottom){
 unsigned changed=0;
 for(int y=ht_max(0,t);y<=ht_min(HT_NATIVE_H-1,bottom);++y)
  for(int x=ht_max(0,l);x<=ht_min(HT_NATIVE_W-1,r);++x){
   int at=y*(HT_NATIVE_W/8)+x/8;
   changed+=((a[at]^b[at])&(0x80u>>(x&7)))!=0;
  }
 return changed;
}
int main(void){
 uint8_t *mem=malloc(HT_MEMORY+HT_NATIVE_MEMORY),*copy=malloc(HT_NATIVE_PIXELS);
 uint8_t *bits=malloc(HT_NATIVE_PIXELS/8),*other=malloc(HT_NATIVE_PIXELS/8);
 assert(mem && copy && bits && other);ht_bind(mem);ht_bind_native(mem);
 memset(&ht,0,sizeof(ht));ht.level=5;ht_select_level(5);ht_spawn(true);
 unsigned contacts=0,ladders=0,frames=0;int previous_mode=HT_FREE;
 for(unsigned step=0;step<20000 && ht.level==5;++step){
  walk_route_tick();
  if(ht.traversal.mode==HT_LADDER && previous_mode!=HT_LADDER)++ladders;
  previous_mode=ht.traversal.mode;
  if(ht_quarry_cloth_contact(&ht)){
   ++contacts;ht_game live=ht;
   for(int direction=-1;direction<=1;direction+=2){
    ht_game view=ht;view.facing=direction;
    ht_person_pose pose=ht_person_pose_at(view.x/256-view.camera/256,(view.y-view.camera_y)/256,&view);
    int wx=pose.hand[0].x+view.camera/256;
    assert(wx==view.x/256+direction*5);
    assert(pose.hand[0].y+view.camera_y/256==ht_quarry_cloth_y(wx));
    int dx=pose.hand[0].x-pose.shoulder.x,dy=pose.hand[0].y-pose.shoulder.y;
    assert(dx*dx+dy*dy<17*17);
   }
   assert(!memcmp(&ht,&live,sizeof(ht)));
  }
  if(ht.level==5 && ht.grounded && (ht.x/256==1100 || ht.x/256==1919)){
   for(unsigned mode=0;mode<2;++mode){
    ht_camera_mode=mode?HT_CAMERA_NATIVE:HT_CAMERA_BASELINE;ht_game frozen=ht;
    services=0;ht_service=service;ht_render_scene();
    assert(services>0 && services<3000);
    size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;memcpy(copy,ht_scene,bytes);ht_pack_mono(bits,120);
    ht_render_scene();ht_pack_mono(other,120);
    assert(!memcmp(copy,ht_scene,bytes) && !memcmp(bits,other,HT_NATIVE_PIXELS/8));
    assert(!memcmp(&ht,&frozen,sizeof(ht)));++frames;
   }
  }
 }
 assert(ht.level==6 && !ht.deaths && contacts>10 && ladders==3 && frames>=2);
 assert((ht.evidence&(7u<<15))==(7u<<15));
 ht_service=NULL;
 /* Every possible held contact is on support three; a body below it, in the
  * air, on a ladder, or on any other chapter keeps its original activity. */
 memset(&ht,0,sizeof(ht));ht.level=5;ht_select_level(5);ht_spawn(true);
 for(int x=HT_QUARRY_CLOTH_LEFT+6;x<=HT_QUARRY_CLOTH_RIGHT-6;++x){
  ht.x=x*256;ht.y=ht_surface_at(&ht,3,x)*256;ht.grounded=true;ht.traversal.mode=HT_FREE;
  assert(ht_quarry_cloth_contact(&ht));ht.y+=120*256;assert(!ht_quarry_cloth_contact(&ht));ht.y-=120*256;
  ht.grounded=false;assert(!ht_quarry_cloth_contact(&ht));ht.grounded=true;
  ht.traversal.mode=HT_LADDER;assert(!ht_quarry_cloth_contact(&ht));
 }
 for(unsigned mode=0;mode<3;++mode){
  ht_bind(mem);ht_bind_native(mem);ht.level=5;ht_spawn(true);ht.camera=940*256;ht.camera_y=-150*256;
  ht_native_active=mode!=0;ht_native_foreground_half_y=mode==2;ht_scene=mode?ht_native_a:ht_scene_low;ht_world_scale=256;
  size_t bytes=mode?HT_NATIVE_PIXELS:HT_PIXELS;
  memset(ht_scene,255,bytes);ht_game frozen=ht;ht_quarry_ascent_traces(&ht);
  assert(!memcmp(&ht,&frozen,sizeof(ht)));ht_pack_mono(bits,120);
  /* Erasing only the three small impressions changes three separate regions
   * in the actual mono packer. The adult tracks and cloth remain intact. */
  for(size_t i=0;i<bytes;++i)if(ht_scene[i]==202 || ht_scene[i]==196)ht_scene[i]=82;
  ht_pack_mono(other,120);
  const int print_x[3]={1080,1097,1114};
  for(unsigned i=0;i<3;++i){int x=print_x[i]-940,y=ht_surface_at(&ht,3,print_x[i])+151;
   assert(pixels_changed(bits,other,(x-4)*2,(y-2)*2,(x+5)*2,(y+3)*2)>0);
  }
  /* Tank roofs, silver marsh and railway each survive production packing. */
  ht.camera=1690*256;ht.camera_y=-120*256;memset(ht_scene,81,bytes);
  ht_quarry_country(&ht);ht_pack_mono(bits,120);memcpy(copy,ht_scene,bytes);
  const int tones[3]={184,33,166};
  for(unsigned feature=0;feature<3;++feature){
   memcpy(ht_scene,copy,bytes);unsigned count=0;
   for(size_t i=0;i<bytes;++i)if(ht_scene[i]==tones[feature]){ht_scene[i]=119;++count;}
   assert(count>0);ht_pack_mono(other,120);
   assert(pixels_changed(bits,other,0,0,959,539)>0);
  }
 }
 /* Both scenery functions are exactly absent outside the quarry. */
 for(unsigned level=0;level<HT_LEVELS;++level)if(level!=5){
  ht.level=level;ht.camera=1690*256;ht.grounded=true;ht.traversal.mode=HT_FREE;
  assert(!ht_quarry_cloth_contact(&ht));memset(ht_scene,81,HT_NATIVE_PIXELS);memcpy(copy,ht_scene,HT_NATIVE_PIXELS);
  ht_game frozen=ht;ht_quarry_country(&ht);ht_quarry_ascent_traces(&ht);
  assert(!memcmp(copy,ht_scene,HT_NATIVE_PIXELS) && !memcmp(&frozen,&ht,sizeof(ht)));
 }
 free(other);free(bits);free(copy);free(mem);
 puts("Quarry ascent: actual three-ladder route, cloth contacts, unchanged state, both render paths, three distinct prints and country features in packed mono PASS");
}
