/* Fused row kernels for the established AI renderer. No scene-specific paths.
 * Pure S3 PIE arithmetic, no peripheral or new firmware API. All leaves preserve
 * SAR; compositor also preserves SAR_BYTE (user register 13). QR registers are
 * scratch. Stack offsets 0..15 are local; upper 16 bytes are ABI spill space.
 * Every caller bounds a span to one row, handles tails and yields between rows.
 * Host routines below are reference models, never hardware timing substitutes.
 */
enum {HT_OPT_UPSCALE=1,HT_OPT_RECON=2,HT_OPT_BLEND=4,HT_OPT_BLUR=8,
      HT_OPT_CAMERA=16,HT_OPT_VIGNETTE=32,HT_OPT_TRIANGLE=64,HT_OPT_FOCUS=128,HT_OPT_FLAT_CAMERA=256,HT_OPT_NN_CACHE=512,HT_OPT_BLUR_INTERIOR=1024,HT_OPT_TILE_PLAN=2048,HT_OPT_SIMD_ALL=15};
enum {HT_TEST_BASE,HT_TEST_UPSCALE,HT_TEST_RECON,HT_TEST_BLEND,HT_TEST_BLUR,
      HT_TEST_CAMERA,HT_TEST_VIGNETTE,HT_TEST_TRIANGLE,HT_TEST_ALL_FOUR,HT_TEST_FOCUS,HT_TEST_FLAT_CAMERA,HT_TEST_NN_CACHE,HT_TEST_BLUR_INTERIOR,HT_TEST_TILE_PLAN,HT_TEST_COUNT};
static unsigned ht_render_test,ht_simd_stage_ready;
static bool ht_expanded_ready;
static const char *ht_stage_reason[4];
static unsigned ht_render_test_mask(void) {
    switch(ht_render_test) {
    case HT_TEST_UPSCALE:return HT_OPT_UPSCALE;
    case HT_TEST_RECON:return HT_OPT_RECON;
    case HT_TEST_BLEND:return HT_OPT_BLEND;
    case HT_TEST_BLUR:return HT_OPT_BLUR;
    case HT_TEST_CAMERA:return HT_OPT_CAMERA;
    case HT_TEST_VIGNETTE:return HT_OPT_VIGNETTE;
    case HT_TEST_TRIANGLE:return HT_OPT_TRIANGLE;
    case HT_TEST_ALL_FOUR:return HT_OPT_SIMD_ALL;
    case HT_TEST_FOCUS:return HT_OPT_FOCUS;
    case HT_TEST_FLAT_CAMERA:return HT_OPT_FLAT_CAMERA;
    case HT_TEST_NN_CACHE:return HT_OPT_NN_CACHE;
    case HT_TEST_BLUR_INTERIOR:return HT_OPT_BLUR_INTERIOR;
    case HT_TEST_TILE_PLAN:return HT_OPT_TILE_PLAN;
    default:return 0;
    }
}
static const char *ht_render_test_name(void) {
    switch(ht_render_test) {
    case HT_TEST_UPSCALE:return "SIMD UPSCALE";
    case HT_TEST_RECON:return "SIMD RECONSTRUCTION";
    case HT_TEST_BLEND:return "SIMD COMPOSITING";
    case HT_TEST_BLUR:return "SIMD BLUR / CACHE";
    case HT_TEST_CAMERA:return "PACKED CAMERA";
    case HT_TEST_VIGNETTE:return "PACKED VIGNETTE";
    case HT_TEST_TRIANGLE:return "TRIANGLE STEPPING";
    case HT_TEST_ALL_FOUR:return "ALL FOUR SIMD";
    case HT_TEST_FOCUS:return "BOUNDED FOCUS MATH";
    case HT_TEST_FLAT_CAMERA:return "FLAT CAMERA SKIP";
    case HT_TEST_NN_CACHE:return "NEURAL PATCH CACHE";
    case HT_TEST_BLUR_INTERIOR:return "BLUR INTERIOR";
    case HT_TEST_TILE_PLAN:return "COMPOSITOR TILE PLAN";
    default:return "PACKING BASELINE";
    }
}
static bool ht_expanded_active(unsigned bit) {
    return (ht_render_test_mask() & ht_simd_stage_ready & bit)!=0;
}
static bool ht_render_test_available(void) {
    unsigned need=ht_render_test_mask()&HT_OPT_SIMD_ALL;
    return (ht_simd_stage_ready&need)==need;
}
static void ht_render_test_next(void) {ht_render_test=(ht_render_test+1)%HT_TEST_COUNT;}
/* Runtime-aligned PSRAM, not ELF .rodata: section relocation can discard
 * linker alignment. Initialized once by ht_bind, shared by checks and draws. */
static uint8_t (*ht_expand_constants)[16];
#if defined(__XTENSA__)
extern void ht_expand_span_s3(const uint8_t *,const uint8_t *,uint8_t *,uint8_t *,const uint8_t *,unsigned);
extern void ht_edge_span_s3(const uint8_t *,const uint8_t *,uint8_t *,const uint8_t *,unsigned);
extern void ht_blend_span_s3(const uint8_t *,const uint8_t *,const uint16_t *,uint8_t *,const uint16_t *,unsigned);
extern void ht_blur_span_s3(int16_t *,const int16_t *,const int16_t *,uint8_t *,const uint16_t *,unsigned);
__asm__(
 ".text\n"
 ".align 4\n"
 ".global ht_expand_span_s3\n"
 ".hidden ht_expand_span_s3\n"
 ".type ht_expand_span_s3,@function\n"
 "ht_expand_span_s3:\n"
 "entry a1,32\n"
 "rsr a8,sar\n"
 "s32i a8,a1,0\n"
 "movi a8,1\n"
 "wsr a8,sar\n"
 "ee.vld.128.ip q6,a6,16\n"
 "ee.vld.128.ip q7,a6,16\n"
 "1:\n"
 "ee.vld.l.64.ip q0,a2,8\n"
 "ee.vld.h.64.ip q0,a2,8\n"
 "ee.orq q2,q0,q0\n"
 "l8ui a8,a2,0\n"
 "ee.movi.32.q q4,a8,0\n"
 "ee.srci.2q q4,q2,0\n"
 "ee.vld.l.64.ip q1,a3,8\n"
 "ee.vld.h.64.ip q1,a3,8\n"
 "ee.orq q3,q1,q1\n"
 "l8ui a8,a3,0\n"
 "ee.movi.32.q q4,a8,0\n"
 "ee.srci.2q q4,q3,0\n"
 "ee.xorq q0,q0,q6\n"
 "ee.xorq q1,q1,q6\n"
 "ee.xorq q2,q2,q6\n"
 "ee.xorq q3,q3,q6\n"
 "ee.xorq q4,q0,q2\n"
 "ee.andq q5,q0,q2\n"
 "ee.vsr.32 q2,q4\n"
 "ee.andq q2,q2,q7\n"
 "ee.andq q4,q4,q6\n"
 "ee.orq q2,q2,q4\n"
 "ee.vadds.s8 q2,q2,q5\n"
 "ee.xorq q4,q1,q3\n"
 "ee.andq q5,q1,q3\n"
 "ee.vsr.32 q3,q4\n"
 "ee.andq q3,q3,q7\n"
 "ee.andq q4,q4,q6\n"
 "ee.orq q3,q3,q4\n"
 "ee.vadds.s8 q3,q3,q5\n"
 "ee.xorq q4,q0,q1\n"
 "ee.andq q5,q0,q1\n"
 "ee.vsr.32 q1,q4\n"
 "ee.andq q1,q1,q7\n"
 "ee.andq q4,q4,q6\n"
 "ee.orq q1,q1,q4\n"
 "ee.vadds.s8 q1,q1,q5\n"
 "ee.xorq q4,q2,q3\n"
 "ee.andq q5,q2,q3\n"
 "ee.vsr.32 q3,q4\n"
 "ee.andq q3,q3,q7\n"
 "ee.andq q4,q4,q6\n"
 "ee.orq q3,q3,q4\n"
 "ee.vadds.s8 q3,q3,q5\n"
 "ee.xorq q0,q0,q6\n"
 "ee.xorq q1,q1,q6\n"
 "ee.xorq q2,q2,q6\n"
 "ee.xorq q3,q3,q6\n"
 "ee.vzip.8 q0,q2\n"
 "ee.vzip.8 q1,q3\n"
 "ee.vst.128.ip q0,a4,16\n"
 "ee.vst.128.ip q2,a4,16\n"
 "ee.vst.128.ip q1,a5,16\n"
 "ee.vst.128.ip q3,a5,16\n"
 "addi a7,a7,-1\n"
 "bnez a7,1b\n"
 "l32i a8,a1,0\n"
 "wsr a8,sar\n"
 "retw\n"
 ".size ht_expand_span_s3,.-ht_expand_span_s3\n"
);
__asm__(
 ".text\n"
 ".align 4\n"
 ".global ht_edge_span_s3\n"
 ".hidden ht_edge_span_s3\n"
 ".type ht_edge_span_s3,@function\n"
 "ht_edge_span_s3:\n"
 "entry a1,32\n"
 "rsr a8,sar\n"
 "s32i a8,a1,0\n"
 "ee.vld.128.ip q6,a5,32\n"
 "ee.vld.128.ip q7,a5,0\n"
 "1:\n"
 "ee.vld.l.64.ip q0,a2,8\n"
 "ee.vld.h.64.ip q0,a2,8\n"
 "ee.orq q2,q0,q0\n"
 "l8ui a8,a2,0\n"
 "ee.movi.32.q q4,a8,0\n"
 "ee.srci.2q q4,q2,0\n"
 "ee.vld.l.64.ip q1,a3,8\n"
 "ee.vld.h.64.ip q1,a3,8\n"
 "ee.orq q3,q1,q1\n"
 "l8ui a8,a3,0\n"
 "ee.movi.32.q q4,a8,0\n"
 "ee.srci.2q q4,q3,0\n"
 "ee.xorq q0,q0,q6\n"
 "ee.xorq q1,q1,q6\n"
 "ee.xorq q2,q2,q6\n"
 "ee.xorq q3,q3,q6\n"
 "ee.vmin.s8 q4,q0,q1\n"
 "ee.vmax.s8 q5,q0,q1\n"
 "ee.vmin.s8 q4,q4,q2\n"
 "ee.vmax.s8 q5,q5,q2\n"
 "ee.vmin.s8 q4,q4,q3\n"
 "ee.vmax.s8 q5,q5,q3\n"
 "ee.vsubs.s8 q5,q5,q4\n"
 "ee.vcmp.gt.s8 q5,q5,q7\n"
 "ee.vst.128.ip q5,a4,16\n"
 "addi a6,a6,-1\n"
 "bnez a6,1b\n"
 "l32i a8,a1,0\n"
 "wsr a8,sar\n"
 "retw\n"
 ".size ht_edge_span_s3,.-ht_edge_span_s3\n"
);
__asm__(
 ".text\n"
 ".align 4\n"
 ".global ht_blend_span_s3\n"
 ".hidden ht_blend_span_s3\n"
 ".type ht_blend_span_s3,@function\n"
 "ht_blend_span_s3:\n"
 "entry a1,32\n"
 "rsr a8,sar\n"
 "s32i a8,a1,0\n"
 "rur.sar_byte a8\n"
 "s32i a8,a1,4\n"
 "movi a8,8\n"
 "wsr a8,sar\n"
 "ee.vld.128.ip q3,a6,16\n"
 "ee.vld.128.ip q4,a6,0\n"
 "1:\n"
 "mov a8,a2\n"
 "ee.ld.128.usar.ip q0,a8,16\n"
 "ee.vld.128.ip q5,a8,16\n"
 "ee.src.q q0,q0,q5\n"
 "ee.vld.128.ip q6,a8,0\n"
 "ee.src.q q5,q5,q6\n"
 "ee.vunzip.8 q0,q5\n"
 "ee.vunzip.8 q0,q5\n"
 "ee.zero.q q5\n"
 "ee.vzip.8 q0,q5\n"
 "mov a8,a3\n"
 "ee.ld.128.usar.ip q1,a8,16\n"
 "ee.vld.128.ip q5,a8,16\n"
 "ee.src.q q1,q1,q5\n"
 "ee.vld.128.ip q6,a8,0\n"
 "ee.src.q q5,q5,q6\n"
 "ee.vunzip.8 q1,q5\n"
 "ee.vunzip.8 q1,q5\n"
 "ee.zero.q q5\n"
 "ee.vzip.8 q1,q5\n"
 "mov a8,a4\n"
 "ee.vld.l.64.ip q2,a8,8\n"
 "ee.vld.h.64.ip q2,a8,8\n"
 "ee.vsubs.s16 q1,q1,q0\n"
 "ee.vmul.s16 q1,q1,q2\n"
 "ee.vadds.s16 q0,q0,q1\n"
 "ee.vmul.u16 q2,q2,q3\n"
 "ee.vsubs.s16 q2,q4,q2\n"
 "ee.vmul.u16 q0,q0,q2\n"
 "l32i a8,a5,0\n"
 "ee.movi.32.q q5,a8,0\n"
 "l32i a8,a5,4\n"
 "ee.movi.32.q q5,a8,1\n"
 "ee.zero.q q6\n"
 "ee.vzip.8 q5,q6\n"
 "ee.vmax.s16 q0,q0,q5\n"
 "ee.zero.q q6\n"
 "ee.vunzip.8 q0,q6\n"
 "ee.movi.32.a q0,a8,0\n"
 "ee.movi.32.a q0,a9,1\n"
 "s32i a8,a5,0\n"
 "s32i a9,a5,4\n"
 "addi a2,a2,32\n"
 "addi a3,a3,32\n"
 "addi a4,a4,16\n"
 "addi a5,a5,8\n"
 "addi a7,a7,-1\n"
 "bnez a7,1b\n"
 "l32i a8,a1,4\n"
 "wur.sar_byte a8\n"
 "l32i a8,a1,0\n"
 "wsr a8,sar\n"
 "retw\n"
 ".size ht_blend_span_s3,.-ht_blend_span_s3\n"
);
__asm__(
 ".text\n"
 ".align 4\n"
 ".global ht_blur_span_s3\n"
 ".hidden ht_blur_span_s3\n"
 ".type ht_blur_span_s3,@function\n"
 "ht_blur_span_s3:\n"
 "entry a1,32\n"
 "rsr a8,sar\n"
 "s32i a8,a1,0\n"
 "ee.vld.128.ip q3,a6,16\n"
 "ee.vld.128.ip q4,a6,0\n"
 "1:\n"
 "movi a8,16\n"
 "wsr a8,sar\n"
 "ee.vld.128.ip q0,a2,0\n"
 "ee.vmul.u16 q5,q0,q3\n"
 "movi a8,0\n"
 "wsr a8,sar\n"
 "ee.vmul.u16 q6,q5,q4\n"
 "ee.vcmp.gt.s16 q6,q6,q0\n"
 "ee.vadds.s16 q5,q5,q6\n"
 "ee.zero.q q6\n"
 "ee.vunzip.8 q5,q6\n"
 "ee.vst.l.64.ip q5,a5,8\n"
 "ee.vld.128.ip q1,a3,16\n"
 "ee.vld.128.ip q2,a4,16\n"
 "ee.vadds.s16 q0,q0,q1\n"
 "ee.vsubs.s16 q0,q0,q2\n"
 "ee.vst.128.ip q0,a2,16\n"
 "addi a7,a7,-1\n"
 "bnez a7,1b\n"
 "l32i a8,a1,0\n"
 "wsr a8,sar\n"
 "retw\n"
 ".size ht_blur_span_s3,.-ht_blur_span_s3\n"
);
#endif
/* Pure C oracle for each leaf; it intentionally states the original equations. */
static void ht_expand_span_c(const uint8_t *a,const uint8_t *b,uint8_t *top,uint8_t *bottom,unsigned blocks) {
    for(unsigned i=0;i<blocks*16;++i) {
        unsigned h=(a[i]+a[i+1])/2,hb=(b[i]+b[i+1])/2;
        top[2*i]=a[i];top[2*i+1]=(uint8_t)h;
        bottom[2*i]=(uint8_t)((a[i]+b[i])/2);bottom[2*i+1]=(uint8_t)((h+hb)/2);
    }
}
static void ht_edge_span_c(const uint8_t *a,const uint8_t *b,uint8_t *mask,unsigned blocks) {
    for(unsigned i=0;i<blocks*16;++i) {
        int lo=ht_min(ht_min(a[i],a[i+1]),ht_min(b[i],b[i+1]));
        int hi=ht_max(ht_max(a[i],a[i+1]),ht_max(b[i],b[i+1]));
        mask[i]=hi-lo>=80?255:0;
    }
}
static void ht_blend_span_c(const uint8_t *near,const uint8_t *wide,const uint16_t *weight,
        uint8_t *scene,const uint16_t *k,unsigned blocks) {
    for(unsigned i=0;i<blocks*8;++i) {
        int r=weight[i],n=near[4*i],w=wide[4*i];
        int v=(n*256+(w-n)*r)>>8;
        v=v*(256-((r*k[0])>>8))>>8;
        scene[i]=(uint8_t)ht_max(scene[i],v);
    }
}
static void ht_blur_span_c(int16_t *sums,const int16_t *add,const int16_t *sub,uint8_t *out,
        const uint16_t *k,unsigned blocks) {
    for(unsigned i=0;i<blocks*8;++i) {
        out[i]=(uint8_t)(sums[i]/k[8]);
        sums[i]=(int16_t)(sums[i]+add[i]-sub[i]);
    }
}
static void ht_expand_span(const uint8_t *a,const uint8_t *b,uint8_t *top,uint8_t *bottom,unsigned blocks) {
#if defined(__XTENSA__)
    ht_expand_span_s3(a,b,top,bottom,&ht_expand_constants[0][0],blocks);
#else
    ht_expand_span_c(a,b,top,bottom,blocks);
#endif
}
static void ht_edge_span(const uint8_t *a,const uint8_t *b,uint8_t *mask,unsigned blocks) {
#if defined(__XTENSA__)
    ht_edge_span_s3(a,b,mask,&ht_expand_constants[0][0],blocks);
#else
    ht_edge_span_c(a,b,mask,blocks);
#endif
}
static void ht_blend_span(const uint8_t *n,const uint8_t *w,const uint16_t *r,uint8_t *s,const uint16_t *k,unsigned blocks) {
#if defined(__XTENSA__)
    ht_blend_span_s3(n,w,r,s,k,blocks);
#else
    ht_blend_span_c(n,w,r,s,k,blocks);
#endif
}
static void ht_blur_span(int16_t *s,const int16_t *a,const int16_t *b,uint8_t *d,const uint16_t *k,unsigned blocks) {
#if defined(__XTENSA__)
    ht_blur_span_s3(s,a,b,d,k,blocks);
#else
    ht_blur_span_c(s,a,b,d,k,blocks);
#endif
}
/* Tests actual leaves on the device before enabling any expanded operation.
 * Small fixed buffers, guarded writes, checkpoints every eight patterns.
 * Alignment variants include odd camera offsets and the 120-byte row stride. */
static bool ht_expanded_selftest(unsigned requested) {
#if defined(__XTENSA__)
    ht_expanded_reason="NO MATH API";
    if(!ht_math)return false;
    ht_expanded_reason="NO S3 FEATURE";
    if(!(ht_math->features&T5_MATH_FEATURE_S3_DSP))return false;
    ht_expanded_reason="TABLE ALIGN";
    if(!ht_expand_constants || ((uintptr_t)ht_expand_constants&15u))return false;
    uint8_t a[128] __attribute__((aligned(16))),b[128] __attribute__((aligned(16)));
    uint8_t got[160] __attribute__((aligned(16))),want[160] __attribute__((aligned(16)));
    uint16_t r[40] __attribute__((aligned(16))),k[16] __attribute__((aligned(16)));
    int16_t sums[32] __attribute__((aligned(16))),expected[32] __attribute__((aligned(16)));
    int16_t add[32] __attribute__((aligned(16))),sub[32] __attribute__((aligned(16)));
    for(unsigned pattern=0;pattern<128;++pattern) {
        for(unsigned i=0;i<128;++i) {
            a[i]=(uint8_t)(pattern<16?pattern*17:ht_hash(pattern*97+i));
            b[i]=(uint8_t)(pattern<16?(15-pattern)*17:ht_hash(pattern*173+i));
        }
        for(unsigned i=0;i<40;++i)r[i]=(uint16_t)(pattern<3?pattern*128:ht_hash(pattern+i)%257);
        unsigned blocks=1+(pattern&1),off=(pattern&2)*4;
        if(requested&(HT_OPT_UPSCALE|HT_OPT_RECON)) {
            memset(got,0xa5,sizeof(got));memcpy(want,got,sizeof(got));
            ht_expand_span_c(a+off,b+8-off,want+16,want+80,blocks);
            ht_expand_span(a+off,b+8-off,got+16,got+80,blocks);
            ht_expanded_reason="UPSCALE TEST";
            if(!ht_simd_test_equal(got,want,sizeof(got),pattern,0))return false;
        }
        if(requested&HT_OPT_RECON) {
            memset(got,0xa5,sizeof(got));memcpy(want,got,sizeof(got));
            ht_edge_span_c(a+off,b+8-off,want+16,blocks);
            ht_edge_span(a+off,b+8-off,got+16,blocks);
            ht_expanded_reason="EDGE TEST";
            if(!ht_simd_test_equal(got,want,sizeof(got),pattern,0))return false;
        }
        if(requested&HT_OPT_BLEND) {
            for(int i=0;i<8;++i){k[i]=pattern&1?45:70;k[i+8]=256;}
            for(unsigned i=0;i<sizeof(got);++i)got[i]=(uint8_t)ht_hash(i+pattern);
            memcpy(want,got,sizeof(got));
            off=pattern&15;
            ht_blend_span_c(a+off,b+off,r+(pattern&4),want+16+(pattern&4),k,blocks);
            ht_blend_span(a+off,b+off,r+(pattern&4),got+16+(pattern&4),k,blocks);
            ht_expanded_reason="BLEND TEST";
            if(!ht_simd_test_equal(got,want,sizeof(got),pattern,0))return false;
        }
        if(requested&HT_OPT_BLUR) {
            unsigned n=3+2*(pattern%9);
            for(int i=0;i<8;++i){k[i]=(uint16_t)((65536u+n-1)/n);k[i+8]=(uint16_t)n;}
            for(int i=0;i<32;++i) {
                sub[i]=(int16_t)(ht_hash(i+pattern)%256);add[i]=(int16_t)(ht_hash(i+pattern+99)%256);
                sums[i]=expected[i]=(int16_t)(sub[i]+ht_hash(i+pattern*7)%(255*(n-1)+1));
        }
        memset(got,0xa5,sizeof(got));memcpy(want,got,sizeof(got));
        ht_blur_span_c(expected+8,add+8,sub+8,want+16,k,blocks);
        ht_blur_span(sums+8,add+8,sub+8,got+16,k,blocks);
        ht_expanded_reason="BLUR TEST";
        if(!ht_simd_test_equal(got,want,sizeof(got),pattern,0) ||
           !ht_simd_test_equal(sums,expected,sizeof(sums),pattern,1))return false;
        }
        if((pattern&7)==7)ht_checkpoint();
    }
    ht_expanded_reason="READY";return true;
#else
    (void)requested;ht_expanded_reason="NOT S3 BUILD";return false;
#endif
}
