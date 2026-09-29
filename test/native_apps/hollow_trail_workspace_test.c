/* Memory-domain selection is an allocator contract; host checks verify caps,
 * alignment, failure fallback, row reuse and bit-exact rendering, not S3 speed. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static unsigned allocations,checkpoints;
static bool fail;
static void *allocate(size_t size,uint32_t caps) {
    ++allocations;assert(size==4111 && caps==((1u<<11)|(1u<<2)));
    return fail?NULL:malloc(size);
}
static void service(void){++checkpoints;}
int main(void) {
    uint8_t *mem=malloc(HT_MEMORY),*want=malloc(HT_PIXELS),*raw=malloc(HT_FAST_ALLOC_BYTES+16);
    assert(mem && want && raw);ht_bind(mem);
    for(unsigned align=0;align<16;++align) {
        ht_workspace_attach(raw+align);
        assert(!((uintptr_t)ht_fast&15u));
        assert((uint8_t *)ht_fast>=(raw+align) && (uint8_t *)(ht_fast+1)<=raw+align+HT_FAST_ALLOC_BYTES);
    }
    fail=true;assert(!ht_workspace_open(allocate) && !ht_fast && allocations==1);
    for(unsigned mode=HT_TEST_RAM_NEURAL;mode<=HT_TEST_RAM_PACK;++mode) {
        ht_render_test=mode;assert(!ht_render_test_available());
    }
    fail=false;void *owned=ht_workspace_open(allocate);assert(owned && ht_fast && allocations==2);
    for(unsigned mode=HT_TEST_RAM_NEURAL;mode<=HT_TEST_RAM_PACK;++mode) {
        ht_render_test=mode;assert(ht_render_test_available());
    }
    ht_nn_fast_init();
    for(unsigned n=0;n<30000;++n) {
        uint8_t patch[9];int16_t a[3],b[3];
        for(unsigned i=0;i<9;++i)patch[i]=(uint8_t)ht_hash(n*13+i);
        ht_nn_residual(patch,a);ht_nn_residual_fast(patch,b);assert(!memcmp(a,b,sizeof(a)));
    }
    ht_service=service;
    /* Whole reconstruction, including duplicated border rows and strong edges. */
    for(unsigned n=0;n<16;++n) {
        for(unsigned i=0;i<HT_RECON_PIXELS;++i)ht_recon_scene[i]=(uint8_t)ht_hash(i+n*71);
        ht_render_test=HT_TEST_BASE;checkpoints=0;ht_reconstruct_low_scene();unsigned calls=checkpoints;
        memcpy(want,ht_low_scene,HT_SCENE_PIXELS);
        for(unsigned available=0;available<2;++available) {
            ht_workspace_attach(available?owned:NULL);ht_render_test=HT_TEST_RAM_NEURAL;
            checkpoints=0;ht_reconstruct_low_scene();assert(checkpoints==calls);
            assert(!memcmp(want,ht_low_scene,HT_SCENE_PIXELS));
        }
    }
    /* Full blur stages: output guards, radius zero, both image edges and bands. */
    for(unsigned i=0;i<HT_PIXELS;++i)ht_raw[i]=(uint8_t)ht_hash(i);
    for(unsigned n=0;n<160;++n) {
        int radius=n%10,x0=ht_hash(n)%HT_W,x1=x0+1+ht_hash(n+731)%(HT_W-x0);
        int y0=ht_hash(n+91)%HT_H,y1=ht_min(HT_H,y0+1+(int)(n%65));
        if(n%3==0)x0=0;
        if(n%5==0)x1=HT_W;
        if(n%7==0){y0=0;y1=HT_H;}
        memset(ht_near,0xa5,HT_PIXELS);ht_render_test=HT_TEST_BASE;checkpoints=0;
        ht_blur_rect(ht_raw,ht_near,radius,x0,x1,y0,y1);unsigned calls=checkpoints;
        memcpy(want,ht_near,HT_PIXELS);
        for(unsigned available=0;available<2;++available) {
            ht_workspace_attach(available?owned:NULL);ht_render_test=HT_TEST_RAM_BLUR;
            memset(ht_near,0xa5,HT_PIXELS);checkpoints=0;
            ht_blur_rect(ht_raw,ht_near,radius,x0,x1,y0,y1);assert(checkpoints==calls);
            assert(!memcmp(want,ht_near,HT_PIXELS));
        }
    }
    /* Packed output with padded/unaligned strides, framed corners, last-row
     * extension, baseline and self-test failure fallback. */
    const size_t bytes=128*HT_H*2+32;
    uint8_t *packed=malloc(bytes),*expected=malloc(bytes);assert(packed && expected);
    ht_ai_rendering=true;ht_output_mode=HT_OUTPUT_SIMD;
    for(unsigned n=0;n<16;++n) {
        int stride=HT_W/4+n%5;unsigned shift=n%4;
        for(unsigned i=0;i<HT_PIXELS;++i)ht_scene[i]=(uint8_t)ht_hash(i+n*191);
        ht_framed=n&1;
        for(int y=0;y<HT_H;++y)ht_visible_left[y]=(y<4||y>HT_H-5)?HT_W/2:4+(y%43);
        for(unsigned ready=0;ready<2;++ready) {
            ht_simd_ready=ready;ht_render_test=HT_TEST_BASE;checkpoints=0;
            memset(expected,0xa5,bytes);ht_pack_mono(expected+shift,stride);unsigned calls=checkpoints;
            for(unsigned available=0;available<2;++available) {
                ht_workspace_attach(available?owned:NULL);ht_render_test=HT_TEST_RAM_PACK;checkpoints=0;
                memset(packed,0xa5,bytes);ht_pack_mono(packed+shift,stride);
                assert(checkpoints==calls && !memcmp(packed,expected,bytes));
            }
        }
    }
    ht_service=NULL;ht_workspace_attach(NULL);free(owned);
    assert(!ht_fast && allocations==2);
    free(packed);free(expected);free(raw);free(want);free(mem);
    puts("Internal workspace: allocator caps/failure, alignment, inference, blur, packing and checkpoints PASS");
}
