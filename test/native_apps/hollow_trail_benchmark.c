/* Warm-cache render/pack benchmark. --hashes verifies packed-frame identity;
 * --moving forces focus-map misses to expose the cost during jumps.
 * Compile both revisions with the same flags; host timings are not S3 FPS. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifndef HT_BENCH_ENGINE
#define HT_BENCH_ENGINE "../../Apps/hollow_trail_engine.inc"
#endif
#include HT_BENCH_ENGINE
static unsigned hash(const unsigned char *p,unsigned n) {
    unsigned h=2166136261u;
    while(n--) h=(h^*p++)*16777619u;
    return h;
}
int main(int argc,char **argv) {
    bool moving=argc>1 && !strcmp(argv[1],"--moving");
    bool hashes=argc>1 && !strcmp(argv[1],"--hashes");
    uint8_t *memory=malloc(HT_MEMORY),*out=malloc(64800);
    if(!memory || !out) {free(memory);free(out);return 1;}
    ht_bind(memory);
    double render=0,pack=0;
    int frames=0;
    for(unsigned level=0;level<HT_LEVELS;++level) {
        ht.level=level;ht_spawn(true);ht.x=1200*256;
        ht.camera=1035*256;ht.camera_y=-100*256;
        ht_select_level(level);ht_cache_visible(1035);
        for(unsigned phase=0;phase<2048;phase+=127) {
            ht.sway_phase=phase;ht_render_scene();ht_pack_mono(out,120);
            if(hashes) printf("%u %u %08x\n",level,phase,hash(out,64800));
        }
        for(int i=0;i<50;++i) {
            ht.sway_phase=200+i;
            if(moving) {ht.x=(1200+i%19)*256;ht.y=(150+i%23)*256;}
            clock_t a=clock();ht_render_scene();
            clock_t b=clock();ht_pack_mono(out,120);
            clock_t c=clock();
            render+=b-a;pack+=c-b;++frames;
        }
    }
    fprintf(stderr,"render %.3f pack %.3f total %.3f ms/frame\n",
        render*1000/CLOCKS_PER_SEC/frames,pack*1000/CLOCKS_PER_SEC/frames,
        (render+pack)*1000/CLOCKS_PER_SEC/frames);
    free(out);free(memory);return 0;
}
