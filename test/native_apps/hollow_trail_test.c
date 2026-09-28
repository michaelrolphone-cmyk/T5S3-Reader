/* Host gameplay and memory-safety smoke test; no firmware/hardware mock. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../Apps/hollow_trail_engine.inc"

static uint32_t checksum(const uint8_t *data,size_t size) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<size;++i) h=(h^data[i])*16777619u;
    return h;
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY), *frame=malloc(960u*540u/4u);
    assert(memory && frame); ht_bind(memory); ht_spawn(true);
    /* Walk the entire route using the same fixed-step physics and hold jump
     * from each takeoff. A route that silently respawns cannot pass. */
    for(int tick=0;tick<2200 && !ht.laps;++tick) {
        bool jump=false;
        for(int i=0;i<HT_PLATFORMS-1;++i)
            if(ht.grounded && ht.x/256>=ht_land[i].right-8 && ht.x/256<=ht_land[i].right+4) jump=true;
        ht_step(1,jump,true);
    }
    assert(ht.laps==1 && ht.deaths==0);
    assert(ht.x==95*256 && ht.checkpoint==0);
    /* A failed jump returns to the latest checkpoint, not the start. */
    ht.checkpoint=6; ht.x=2180*256; ht.y=330*256; ht.vy=2400;
    ht_step(0,false,false);
    assert(ht.deaths==1 && ht.checkpoint==6);
    assert(ht.x==(ht_land[6].left+35)*256 && ht.grounded);
    /* Walk off without jumping: every pit must actually be lethal. */
    for(int i=0;i<HT_PLATFORMS-1;++i) {
        ht_spawn(true); ht.x=(ht_land[i].right-6)*256;
        ht.y=ht_land[i].top*256; ht.vx=640;
        unsigned deaths=ht.deaths;
        for(int t=0;t<100 && ht.deaths==deaths;++t) ht_step(1,false,false);
        assert(ht.deaths==deaths+1);
    }
    /* Render representative camera positions twice: deterministic, bounds
     * checked under ASan/UBSan, all four native tones represented. */
    const int positions[]={95,330,950,1660,2310,3130};
    /* Exhaustive normalization/quantization checks retain original arithmetic. */
    for(unsigned n=1;n<=19;++n) for(unsigned sum=0;sum<=255*n;++sum)
        assert(ht_average((int)sum,(0x1000000u+n-1u)/n)==sum/n);
    for(int value=0;value<256;++value) for(int rank=0;rank<64;++rank) {
        int q=value/85,rem=value-q*85;
        if(q<3 && rem*64>rank*85+42) ++q;
        assert(ht_quantize(value,rank)==q);
    }
    /* Cached shifts must equal a full fresh convolution, including reversals,
     * culling boundaries, both blur footprints, and large camera teleports. */
    uint8_t *expected=malloc(HT_PIXELS); assert(expected);
    for(int i=0;i<100;++i) {
        int camera=i<40?i*7:i<80?(79-i)*7:(i*137)%2860;
        ht.camera=camera*256; ht.x=(camera+165)*256;
        ht.y=(170+i%40)*256;
        ht_render_scene(); memcpy(expected,ht_scene,HT_PIXELS);
        memset(ht_cache_valid,0,sizeof(ht_cache_valid)); ht_render_scene();
        assert(!memcmp(expected,ht_scene,HT_PIXELS));
    }
    /* Cropped foreground convolution must retain the full filter's pixels. */
    for(int i=0;i<HT_PIXELS;++i) ht_raw[i]=(uint8_t)ht_hash((uint32_t)i);
    ht_blur_region(ht_raw,expected,4,0,HT_W);
    ht_blur_rect(ht_raw,ht_wide,4,HT_BORDER,HT_W-HT_BORDER,HT_BORDER,HT_H-HT_BORDER);
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)
        assert(!memcmp(expected+y*HT_W+HT_BORDER,ht_wide+y*HT_W+HT_BORDER,HT_W-2*HT_BORDER));
    /* Upscaling retains exact samples, clamps last rows/columns, and cannot
     * overflow on white/black transitions. Reference is per output pixel. */
    for(int i=0;i<HT_SCENE_PIXELS;++i) ht_low_scene[i]=(uint8_t)ht_hash((uint32_t)i);
    ht_upscale_scene();
    for(int y=0;y<HT_H;++y) for(int x=0;x<HT_W;++x) {
        int sx=x/2,sy=y/2,nx=ht_min(sx+1,HT_SCENE_W-1),ny=ht_min(sy+1,HT_SCENE_H-1);
        int a=ht_low_scene[sy*HT_SCENE_W+sx],b=ht_low_scene[ny*HT_SCENE_W+sx];
        if(x%2) { a=(a+ht_low_scene[sy*HT_SCENE_W+nx])/2; b=(b+ht_low_scene[ny*HT_SCENE_W+nx])/2; }
        assert(ht_scene[y*HT_W+x]==(y%2?(a+b)/2:a));
    }
    /* Border and fade are symmetric and leave the central scene unchanged. */
    memset(ht_scene,100,HT_PIXELS); ht_vignette();
    for(int y=0;y<HT_H;++y) for(int x=0;x<HT_W;++x) {
        int ex=ht_min(x,HT_W-1-x),ey=ht_min(y,HT_H-1-y),edge=ht_min(ex,ey);
        if(ex<HT_MASK_RADIUS && ey<HT_MASK_RADIUS) {
            int dx=HT_MASK_RADIUS-ex,dy=HT_MASK_RADIUS-ey,d=dx*dx+dy*dy,r=0;
            while((r+1)*(r+1)<=d) ++r;
            edge=HT_MASK_RADIUS-r;
        }
        int alpha=ht_clamp(edge-HT_BORDER,0,HT_FADE-HT_BORDER)*255/(HT_FADE-HT_BORDER);
        int wanted=255-(155*alpha+127)/255;
        assert(ht_scene[y*HT_W+x]==wanted);
    }
    /* Fast packing of hidden pixels equals the generic packer, in both modes. */
    uint8_t *generic=malloc(960u*540u/4u); assert(generic);
    for(int mono=0;mono<2;++mono) {
        int stride=mono?120:240;
        ht_framed=true; ht_pack_format(frame,stride,mono);
        ht_framed=false; ht_pack_format(generic,stride,mono);
        assert(!memcmp(frame,generic,(size_t)stride*540u));
    }
    free(generic);
    free(expected);
    clock_t start=clock();
    for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);++i) {
        ht_spawn(true); ht.x=positions[i]*256;
        ht.camera=ht_clamp(positions[i]-165,0,HT_GOAL-340)*256;
        ht_render_scene();
        ht_pack_mono(frame,120);
        assert(ht_dirty_top>0 && ht_dirty_height>0 && ht_dirty_top+ht_dirty_height<540);
        for(unsigned y=0;y<540;++y) if(y<ht_dirty_top || y>=ht_dirty_top+ht_dirty_height)
            for(int x=0;x<120;++x) assert(frame[y*120+x]==255);
        ht_pack(frame,240);
        uint32_t first=checksum(frame,960u*540u/4u);
        ht_render_scene(); ht_pack(frame,240);
        assert(first==checksum(frame,960u*540u/4u));
        unsigned tones=0;
        for(size_t n=0;n<960u*540u/4u;++n) for(int shift=0;shift<8;shift+=2)
            tones|=1u<<((frame[n]>>shift)&3);
        assert(tones==15);
    }
    printf("Hollow Trail: complete route, loop, checkpoints, all pits, deterministic 2bpp frames PASS (%.1f ms/host frame)\n",
           (double)(clock()-start)*1000.0/CLOCKS_PER_SEC/12.0);
    free(frame); free(memory); return 0;
}
