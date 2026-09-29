/* Output contract independent of four-lane packing: phase, anchors, learned
 * classification, edge extension, stride padding and both pointer alignments. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned checkpoints;
static void service(void){++checkpoints;}
static int scalar(int x,int y){
 int sx=x/2,sy=y/2,xp=ht_min(sx+1,HT_W-1),yp=ht_min(sy+1,HT_H-1);
 unsigned a=ht_scene[sy*HT_W+sx],b=ht_scene[sy*HT_W+xp];
 unsigned c=ht_scene[yp*HT_W+sx],d=ht_scene[yp*HT_W+xp];
 int lo=ht_min(ht_min(a,b),ht_min(c,d)),hi=ht_max(ht_max(a,b),ht_max(c,d));
 static const int e[4][4]={{3,51,15,63},{35,19,47,31},{11,59,7,55},{43,27,39,23}};
 if(!(x&1)&&!(y&1))return a>=(unsigned)e[sy&3][sx&3];
 if(hi-lo>=HT_NN_EDGE_THRESHOLD){
  unsigned code=((a>>5)<<9)|((b>>5)<<6)|((c>>5)<<3)|(d>>5);
  unsigned phase=(sy&3)*4+(sx&3);
  unsigned bits=(ht_dither_patterns[ht_dither_class[code]][phase/8]>>(phase%8*4))&7;
  return (bits>>((y&1)?((x&1)?2:1):0))&1;
 }
 int h=(a+b)/2,v=(a+c)/2,dd=(h+(c+d)/2)/2;
 int value=(y&1)?((x&1)?dd:v):h;
 int threshold=e[sy&3][sx&3]+((y&1)?((x&1)?64:128):192);
 return value>=threshold;
}
int main(void){
 uint8_t *mem=malloc(HT_MEMORY),*a=malloc(64800),*raw=malloc(125*540+32);
 assert(mem&&a&&raw);ht_bind(mem);ht_service=service;ht_framed=false;
 for(int tone=0;tone<256;++tone){
  memset(ht_scene,tone,HT_PIXELS);ht_pack_mono_legacy(a,120);ht_pack_mono_learned(raw,120);
  assert(!memcmp(a,raw,64800));
 }
 for(int pattern=0;pattern<3;++pattern){
  for(int y=0;y<HT_H;++y)for(int x=0;x<HT_W;++x)
   ht_scene[y*HT_W+x]=(uint8_t)(pattern==0?ht_hash((unsigned)(y*HT_W+x)):
       (unsigned)(pattern==1?((x/3+y/5)&1)*255:(x+y)%256));
  for(int offset=0;offset<2;++offset)for(int stride=120;stride<=125;stride+=5){
   memset(raw,0xa5,125*540+32);uint8_t *out=raw+16+offset;
   ht_pack_mono_learned(out,stride);
   for(int y=0;y<540;++y){
    for(int x=0;x<960;++x)assert(((out[y*stride+x/8]>>(7-(x&7)))&1)==scalar(x,y));
    for(int x=120;x<stride;++x)assert(out[y*stride+x]==0xa5);
   }
   for(int i=0;i<16+offset;++i)assert(raw[i]==0xa5);
   for(int i=16+offset+stride*540;i<125*540+32;++i)assert(raw[i]==0xa5);
  }
  ht_pack_mono_learned(a,120);ht_pack_mono_learned(raw,120);assert(!memcmp(a,raw,64800));
  ht_vignette();ht_pack_mono_learned(a,120);ht_framed=false;ht_pack_mono_learned(raw,120);
  assert(!memcmp(a,raw,64800));
 }
 assert(checkpoints>0);free(raw);free(a);free(mem);
 puts("Learned dither: scalar output, tones, bounds, alignment, clipping, determinism and service PASS");
}
