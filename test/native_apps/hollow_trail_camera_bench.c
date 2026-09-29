/* Host-only render benchmark: 10 chapters x 3 starts x 100 moving frames x
 * two alternating-order repetitions = 6000 frames per mode. Warmup excluded.
 * No device SIMD packing, PSRAM latency, input servicing or display scans.
 * cc -std=c11 -O2 -Ilib/NativeApps/include test/native_apps/hollow_trail_camera_bench.c -o /tmp/ht-camera-bench
 */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static uint64_t ns(void){struct timespec t;clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
int main(void){uint8_t *mem=malloc(HT_MEMORY);uint64_t duration[4]={0},lo=0,fg=0,sk=0;unsigned full=0;
for(int rep=0;rep<2;++rep)for(unsigned level=0;level<HT_LEVELS;++level)for(int view=0;view<3;++view)for(int order=0;order<4;++order){int mode=rep?3-order:order;
 static const unsigned modes[]={HT_TEST_BASE,HT_TEST_LOW_CAMERA,HT_TEST_OCCLUSION,HT_TEST_FILL_CAMERA};
 ht_bind(mem);ht.level=level;ht_spawn(true);ht.camera=view*733*256;ht.x=(view*733+190)*256;ht.vista=view?256:0;ht.sway_phase=128+view*317;ht.rotation_phase=(128+view*219)<<8;ht.camera_mood=256;
 ht_render_test=modes[mode];ht_render_scene();uint64_t start=ns();
 for(int f=0;f<100;++f){ht.camera=(view*733+f)*256;ht.x=(view*733+f+190)*256;ht_render_scene();if(mode==1){lo+=ht_camera_low_samples;fg+=ht_camera_foreground_samples;sk+=ht_camera_low_skipped;if(!ht_camera_low_samples)++full;}}
 duration[mode]+=ns()-start;
}
static const char *names[]={"baseline","low background camera","coarse occlusion","solid fill camera"};
for(int mode=0;mode<4;++mode)printf("6000 frames: %s %.3f ms ratio %.3f\n",names[mode],duration[mode]/1e6,(double)duration[mode]/duration[0]);printf("low %llu fg %llu skip %llu full/no-low %u\n",(unsigned long long)lo,(unsigned long long)fg,(unsigned long long)sk,full);free(mem);}
