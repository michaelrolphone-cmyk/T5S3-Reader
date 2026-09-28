/* World-strip cache invariants: compare with full viewport geometry and filters,
 * not another cache implementation. Also cover bounded speculative work. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"

static unsigned checkpoints;
static void service(void) { ++checkpoints; }
static void reference(void) {
    const ht_game game=ht;
    int camera=game.camera/256,px=game.x/256-camera,py=game.y/256-14;
    const int radius[]={3,1,0,0},spread[]={9,6,4,4};
    ht_clear_layer(ht_scene);
    for(int depth=0;depth<HT_LAYERS;++depth) {
        ht_clear_layer(ht_raw);
        int offset=ht_layer_offset(depth,camera);
        if(depth<2) ht_background(depth,offset);
        else if(depth==2) ht_foreground(offset);
        else ht_branches(offset);
        ht_blur_region(ht_raw,ht_near,radius[depth],0,HT_W);
        ht_blur_region(ht_near,ht_wide,spread[depth],0,HT_W);
        ht_composite(depth<2?depth:2,px,py);
    }
    ht_character(px,game.y/256,&game); ht_vignette();
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY+32),*expected=malloc(HT_PIXELS);
    assert(memory && expected); memset(memory+HT_MEMORY,0x5a,32);
    ht_bind(memory);ht_spawn(true);ht_service=service;
    int steps=0; while(ht_cache_prefetch(0,1)) assert(++steps<400);
    assert(ht_cache_builds==12 && checkpoints>0);
    unsigned builds=ht_cache_builds;
    /* Moving within warmed strips must not regenerate ANY geometry or blur. */
    for(int camera=0;camera<120;camera+=3) {
        ht.camera=camera*256; ht.x=(camera+165)*256; ht_render_scene();
    }
    assert(ht_cache_builds==builds);
    /* Four bounded work slices per frame keep up with 12px camera steps
     * (roughly walking at the measured 6.6 FPS); all visible data is ready. */
    for(int camera=0;camera<2400;camera+=12) {
        for(int depth=0;depth<HT_LAYERS;++depth) {
            int offset=ht_layer_offset(depth,camera);
            int first=ht_floor_div(offset+HT_BORDER,HT_TILE_W);
            int last=ht_floor_div(offset+HT_W-HT_BORDER-1,HT_TILE_W);
            for(int key=first;key<=last;++key) assert(ht_cache_has(depth,key));
        }
        ht.camera=camera*256; ht.x=(camera+165)*256; ht_render_scene();
        for(int i=0;i<4;++i) if(!ht_cache_prefetch(camera,1)) break;
    }
    const int cameras[]={0,1,22,95,240,255,256,257,460,511,512,900,1711,2860,512,256,0};
    for(unsigned n=0;n<sizeof(cameras)/sizeof(cameras[0]);++n) {
        int camera=cameras[n]; ht.camera=camera*256; ht.x=(camera+165)*256;
        /* Reference uses the work buffers, so cancel speculative work first. */
        ht_cache_job.active=false; reference(); memcpy(expected,ht_scene,HT_PIXELS);
        ht_render_scene();
        if(memcmp(expected,ht_scene,HT_PIXELS)) {
            for(int i=0;i<HT_PIXELS;++i) if(expected[i]!=ht_scene[i]) {
                fprintf(stderr,"camera %d pixel %d,%d expected %d got %d\n",camera,i%HT_W,i/HT_W,expected[i],ht_scene[i]);break;
            }
            assert(0);
        }
        /* Partially prepare rightward work, then reverse; no stale strip use. */
        for(int i=0;i<8;++i) (void)ht_cache_prefetch(camera,1);
        for(int i=0;i<8;++i) (void)ht_cache_prefetch(camera,-1);
        ht_render_scene(); assert(!memcmp(expected,ht_scene,HT_PIXELS));
    }
    for(int i=0;i<32;++i) assert(memory[HT_MEMORY+i]==0x5a);
    ht_abort=true; assert(!ht_cache_prefetch(2000,1)); assert(!ht_cache_visible(2000));
    free(expected);free(memory);
    puts("Hollow Trail cache: warm reuse, full-render equivalence, tile boundaries, reversals, teleports, bounds and cancellation PASS");
}
