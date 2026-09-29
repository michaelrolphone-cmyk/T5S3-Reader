/* Pixel equivalence against the pre-fusion formula, plus an optional host
 * timing comparison. Host timings do not execute ESP-DSP assembly. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../Apps/hollow_trail_engine.inc"

static void reference(int depth,int fx,int fy,uint8_t *dst) {
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y) for(int x=ht_visible_left[y];x<HT_W-ht_visible_left[y];++x) {
        int dx=x-fx,dy=y-fy,i=y*HT_W+x;
        int r=ht_clamp((dx*dx+dy*dy-2500)/200,0,256);
        int v=(ht_near[i]*(256-r)+ht_wide[i]*r)>>8;
        v=v*(256-r*(depth==2?70:45)/256)>>8;
        dst[i]=(uint8_t)ht_max(dst[i],v);
    }
}
static unsigned checkpoints;
static void checkpoint(void) { ++checkpoints; }
static uint32_t hash(const uint8_t *p,size_t n) {
    uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;
}
static unsigned dsp_calls;
static bool reject_dsp;
static bool checked_mul(const int16_t *a,const int16_t *b,int16_t *out,size_t count) {
    assert(!(((uintptr_t)a|(uintptr_t)b|(uintptr_t)out)&15u));
    assert(count>0 && count<=HT_DSP_BATCH);
    ++dsp_calls;
    for(size_t i=0;i<count;++i) assert(a[i]*b[i]>=-32768 && a[i]*b[i]<=32767);
    if(reject_dsp) return false;
    for(size_t i=0;i<count;++i) out[i]=(int16_t)(a[i]*b[i]);
    return true;
}
static const t5_math_api_v1 dsp_mock={.api_version=T5_MATH_API_VERSION,
    .struct_size=sizeof(t5_math_api_v1),.features=T5_MATH_FEATURE_S3_DSP,.mul_s16=checked_mul};
static void dsp_spans(void) {
    uint8_t near[256],wide[256],dst[128],expected[128];
    ht_math=&dsp_mock; assert(ht_dsp_available());
    const int counts[]={1,7,8,15,16,31,63,64};
    for(int failure=0;failure<2;++failure) {
        reject_dsp=failure;
        for(int r=0;r<=256;++r) for(unsigned c=0;c<sizeof(counts)/sizeof(counts[0]);++c) {
            int count=counts[c],fade=(r&1)?45:70;
            for(int i=0;i<256;++i) { near[i]=(uint8_t)(i*67+r);wide[i]=(uint8_t)(255-near[i]); }
            // Explicit extremes and negative interpolation products.
            near[0]=255;wide[0]=0;near[4]=0;wide[4]=255;
            int distance=2500+200*r,delta=(r&1)?-600:4;
            for(int i=0,d=distance,step=delta;i<count;++i,d+=step,step+=32) {
                dst[i]=(uint8_t)(i%113);
                int radial=ht_clamp((d-2500)/200,0,256);
                int v=(near[4*i]*(256-radial)+wide[4*i]*radial)>>8;
                v=v*(256-((radial*fade)>>8))>>8;
                expected[i]=(uint8_t)ht_max(dst[i],v);
            }
            ht_composite_dsp_span(dst,near,wide,count,distance,delta,fade);
            assert(!memcmp(dst,expected,(size_t)count));
        }
    }
    assert(dsp_calls>0);reject_dsp=false;ht_math=NULL;assert(!ht_dsp_available());
}
int main(int argc,char **argv) {
    (void)argv;
    dsp_spans();
    uint8_t *memory=malloc(HT_MEMORY),*expected=malloc(HT_PIXELS);
    assert(memory && expected);ht_bind(memory);
    uint32_t rng=1234567;
    for(int i=0;i<HT_PIXELS;++i) {
        rng=rng*1664525u+1013904223u;ht_near[i]=(uint8_t)(rng>>24);
        rng=rng*1664525u+1013904223u;ht_wide[i]=(uint8_t)(rng>>24);
    }
    const int focus[][2]={{0,0},{50,0},{51,0},{231,0},{232,0},{165,208},
        {479,269},{480,270},{-100,-100},{580,370},{240,135},{95,208}};
    ht_service=checkpoint;
    for(int depth=0;depth<3;++depth) for(unsigned n=0;n<sizeof(focus)/sizeof(focus[0]);++n) {
        for(int i=0;i<HT_PIXELS;++i) expected[i]=ht_scene[i]=(uint8_t)(i%197);
        reference(depth,focus[n][0],focus[n][1],expected);
        checkpoints=0;ht_composite(depth,focus[n][0],focus[n][1]);
        assert(checkpoints>0);
        assert(!memcmp(expected,ht_scene,HT_PIXELS));
    }
    ht_service=NULL;
    if(argc>1) {
        const int iterations=500;
        clock_t start=clock();
        for(int i=0;i<iterations;++i) { memset(expected,0,HT_PIXELS);reference(i%3,95+i%240,208,expected); }
        double old_ms=(double)(clock()-start)*1000/CLOCKS_PER_SEC;
        start=clock();
        for(int i=0;i<iterations;++i) { memset(ht_scene,0,HT_PIXELS);ht_composite(i%3,95+i%240,208); }
        double new_ms=(double)(clock()-start)*1000/CLOCKS_PER_SEC;
        assert(!memcmp(expected,ht_scene,HT_PIXELS));
        printf("Host scalar reference %.2f ms; fused %.2f ms; ratio %.2fx; checksum %08x\n",old_ms,new_ms,old_ms/new_ms,hash(ht_scene,HT_PIXELS));
    }
    free(expected);free(memory);
    puts("Hollow Trail composite: original formula, focus boundaries, all layers, max composition and service checkpoints PASS");
}
