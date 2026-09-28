#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <ctime>
#include <algorithm>
#include "../../src/native/NativeVideoMono.h"
static const uint8_t masks[]={0xfc,0xe0,0x1c,0};
static NativeVideoMonoTable table;
static unsigned original(const uint8_t *src,uint8_t *state,uint8_t *out) {
 bool changed=false,pending=false;
 for(int b=0;b<120;++b) {
  uint8_t incoming=src[b];
  for(int o=0;o<2;++o) {
   uint8_t drive=0;
   for(int p=0;p<2;++p) {
    uint8_t s=*state,dir=incoming>>6,diff=(s^dir)&3;
    changed=changed||diff!=0;s&=masks[diff];s|=dir;
    drive<<=4;drive|=(s&0x80)?0:((dir&2)?4:8);drive|=(s&0x10)?0:((dir&1)?1:2);
    s=(uint8_t)(s+((~s>>2)&0x24));
    if(((s>>5)&7)>=3)s|=0x80;
    if(((s>>2)&7)>=3)s|=0x10;
    pending=pending||((s&0x90)!=0x90);*state++=s;incoming<<=2;
   }
   *out++=drive;
  }
 }
 return changed|(pending<<1);
}
static unsigned lookup(const uint8_t *src,uint8_t *state,uint8_t *out) {
 return table.row(src,state,out,120);
}
static uint8_t src[8][64800],state[259200],other[259200],out[129600],expected[129600];
static double now(){return double(clock())/CLOCKS_PER_SEC;}
int main(int argc,char **) {
 table.init();
 uint32_t rng=12345;
 for(auto &frame:src)for(auto &b:frame){rng=rng*1664525+1013904223;b=rng>>24;}
 // All 1024 state/target combinations, then retargeting/settling sequences.
 for(int s=0;s<256;++s)for(int d=0;d<4;++d){
  uint8_t input[120];memset(input,d*85,120);memset(state,s,480);memcpy(other,state,480);
  unsigned a=original(input,state,out),b=lookup(input,other,expected);
  assert(a==b&&!memcmp(state,other,480)&&!memcmp(out,expected,240));
 }
 memset(state,0,sizeof(state));memset(other,0,sizeof(other));
 for(int f=0;f<40;++f)for(int y=0;y<540;++y){
  int frame=(f/5)%8;
  unsigned a=original(src[frame]+y*120,state+y*480,out+y*240);
  unsigned b=lookup(src[frame]+y*120,other+y*480,expected+y*240);
  assert(a==b);
  assert(!memcmp(state+y*480,other+y*480,480)&&!memcmp(out+y*240,expected+y*240,240));
 }
 puts("All 1024 transitions and 40 full scans match state, drive bytes and flags.");
 if(argc<2) return 0;
 for(int moving=0;moving<2;++moving){
  double times[2][5];unsigned checksum=0;
  for(int trial=0;trial<5;++trial)for(int pass=0;pass<2;++pass){
   int v=(trial+pass)%2;memset(state,0,sizeof(state));auto fn=v?lookup:original;
   double start=now();
   for(int f=0;f<80;++f)for(int y=0;y<540;++y)checksum+=fn(src[moving?f%8:0]+y*120,state+y*480,out+y*240);
   times[v][trial]=(now()-start)*1000/80;
  }
  for(auto &t:times)std::sort(t,t+5);
  printf("%s: current %.3f ms; LUT %.3f ms; %.1f%% less; checksum %u\n",moving?"retarget every scan":"settle unchanged target",times[0][2],times[1][2],100*(1-times[1][2]/times[0][2]),checksum);
 }
 puts("All 1024 transitions plus 40 full scan sequences match state, drive bytes and flags.");
}
