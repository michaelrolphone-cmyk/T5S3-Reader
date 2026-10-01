/* Host CPU comparison only: compile with -Os and the selected revision's Apps include directory. Not ESP32-S3 or display timing. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "hollow_trail_engine.inc"
static unsigned hash(const uint8_t *p,int n) {unsigned h=2166136261u;for(int i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
int main(int argc,char **argv) {
 int repeats=argc>1?atoi(argv[1]):400;
 if(repeats<1 || repeats>10000) return 2;
 puts("camera,vista,render_cpu_ms,pack_cpu_ms,frame_hash");
 uint8_t *mem=malloc(HT_MEMORY),*packed=malloc(120*540);ht_bind(mem);
 const int cameras[]={0,120,240,420,680,700,701,800,899,901};
 for(unsigned n=0;n<sizeof(cameras)/sizeof(cameras[0]);++n) {
  ht.level=4;ht_spawn(true);ht.camera=cameras[n]*256;ht.camera_y=40*256;ht.x=(cameras[n]+190)*256;
  ht.vista=n%2?256:0;ht.sway_phase=512;ht.rotation_phase=256u<<8;
  ht.traversal.boat_x=(398+n*20)*256;ht.ticks=33+n*16;
  ht_render_scene();unsigned sum=hash(ht_scene,HT_PIXELS);
  clock_t begin=clock();for(int i=0;i<repeats;++i)ht_render_scene();double ms=1000.*(clock()-begin)/CLOCKS_PER_SEC/repeats;
  begin=clock();for(int i=0;i<repeats;++i)ht_pack_mono(packed,120);double pack=1000.*(clock()-begin)/CLOCKS_PER_SEC/repeats;
  printf("%d,%u,%.4f,%.4f,%08x\n",cameras[n],ht.vista,ms,pack,sum);
 }
 free(packed);free(mem);
}
