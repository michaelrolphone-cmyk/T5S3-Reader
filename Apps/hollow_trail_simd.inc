/* ESP32-S3 PIE packer: 16 logical pixels -> 32 physical dots in two
 * rows. Exact floor averages and the existing ordered thresholds. Pure CPU
 * arithmetic; no firmware imports, peripheral access, allocation or DMA.
 * S3 instructions follow ESP32-S3 TRM v1.8 sections 1.8.53/76/84/88/191.
 * The leaf preserves SAR in its local stack area; the top 16 bytes remain
 * reserved for the windowed ABI. QR registers are PIE scratch registers.
 * Runtime enable requires the platform's S3 feature flag and a device self-test.
 */
/* Runtime storage follows the renderer arenas. `aligned(16)` on an ELF
 * constant is insufficient: the section loader packs data after a heap block
 * whose base is not guaranteed to preserve the linker's 16-byte alignment. */
static uint8_t (*ht_simd_constants)[6][16];
static void ht_simd_constants_init(uint8_t *memory) {
    ht_simd_constants=(uint8_t (*)[6][16])memory;
    ht_expand_constants=(uint8_t (*)[16])(memory+4*6*16);
    const uint8_t threshold[4][4]={{3,51,15,63},{35,19,47,31},{11,59,7,55},{43,27,39,23}};
    const unsigned offset[4]={0,192,128,64};
    for(unsigned phase=0;phase<4;++phase)for(unsigned lane=0;lane<16;++lane) {
        ht_simd_constants[phase][0][lane]=128;
        ht_simd_constants[phase][1][lane]=127;
        for(unsigned plane=0;plane<4;++plane)
            ht_simd_constants[phase][plane+2][lane]=(uint8_t)((threshold[phase][lane&3]+offset[plane]-1)^128u);
    }
    for(unsigned lane=0;lane<16;++lane) {
        ht_expand_constants[0][lane]=128;ht_expand_constants[1][lane]=127;ht_expand_constants[2][lane]=79;
    }
}
/* The build uses the S3 compiler. Other architectures retain the portable
 * model. The platform feature check prevents executing PIE on another Xtensa. */
#if defined(__XTENSA__)
#define HT_SIMD_S3_COMPILED 1
extern void ht_pack_simd_span_s3(const uint8_t *,const uint8_t *,uint8_t *,uint8_t *,const uint8_t *,unsigned);
__asm__(
 ".text\n"
 ".align 4\n"
 ".global ht_pack_simd_span_s3\n"
 ".hidden ht_pack_simd_span_s3\n"
 ".type ht_pack_simd_span_s3,@function\n"
 "ht_pack_simd_span_s3:\n"
 "entry a1,32\n"
 "rsr a8,sar\n"
 "s32i a8,a1,0\n"
 "movi a8,1\n"
 "wsr a8,sar\n"
 "ee.vld.128.ip q6,a6,16\n"
 "ee.vld.128.ip q7,a6,16\n"
 "mov a13,a6\n"
 "movi a14,0x80808080\n"
 "movi a15,0x80200802\n"
 "1:\n"
 "ee.vld.128.ip q0,a2,16\n"
 "ee.orq q2,q0,q0\n"
 "ee.vld.128.ip q4,a2,0\n"
 "ee.srci.2q q4,q2,0\n"
 "ee.vld.128.ip q1,a3,16\n"
 "ee.orq q3,q1,q1\n"
 "ee.vld.128.ip q4,a3,0\n"
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
 "mov a6,a13\n"
 "ee.vld.128.ip q4,a6,16\n"
 "ee.vcmp.gt.s8 q0,q0,q4\n"
 "ee.vld.128.ip q4,a6,16\n"
 "ee.vcmp.gt.s8 q2,q2,q4\n"
 "ee.vld.128.ip q4,a6,16\n"
 "ee.vcmp.gt.s8 q1,q1,q4\n"
 "ee.vld.128.ip q4,a6,16\n"
 "ee.vcmp.gt.s8 q3,q3,q4\n"
 "ee.movi.32.a q0,a8,0\n"
 "and a8,a8,a14\n"
 "srli a8,a8,7\n"
 "mull a8,a8,a15\n"
 "srli a8,a8,24\n"
 "ee.movi.32.a q2,a9,0\n"
 "and a9,a9,a14\n"
 "srli a9,a9,7\n"
 "mull a9,a9,a15\n"
 "srli a9,a9,25\n"
 "or a8,a8,a9\n"
 "mov a10,a8\n"
 "ee.movi.32.a q0,a8,1\n"
 "and a8,a8,a14\n"
 "srli a8,a8,7\n"
 "mull a8,a8,a15\n"
 "srli a8,a8,24\n"
 "ee.movi.32.a q2,a9,1\n"
 "and a9,a9,a14\n"
 "srli a9,a9,7\n"
 "mull a9,a9,a15\n"
 "srli a9,a9,25\n"
 "or a8,a8,a9\n"
 "slli a8,a8,8\n"
 "or a10,a10,a8\n"
 "ee.movi.32.a q0,a8,2\n"
 "and a8,a8,a14\n"
 "srli a8,a8,7\n"
 "mull a8,a8,a15\n"
 "srli a8,a8,24\n"
 "ee.movi.32.a q2,a9,2\n"
 "and a9,a9,a14\n"
 "srli a9,a9,7\n"
 "mull a9,a9,a15\n"
 "srli a9,a9,25\n"
 "or a8,a8,a9\n"
 "slli a8,a8,16\n"
 "or a10,a10,a8\n"
 "ee.movi.32.a q0,a8,3\n"
 "and a8,a8,a14\n"
 "srli a8,a8,7\n"
 "mull a8,a8,a15\n"
 "srli a8,a8,24\n"
 "ee.movi.32.a q2,a9,3\n"
 "and a9,a9,a14\n"
 "srli a9,a9,7\n"
 "mull a9,a9,a15\n"
 "srli a9,a9,25\n"
 "or a8,a8,a9\n"
 "slli a8,a8,24\n"
 "or a10,a10,a8\n"
 "ee.movi.32.a q1,a8,0\n"
 "and a8,a8,a14\n"
 "srli a8,a8,7\n"
 "mull a8,a8,a15\n"
 "srli a8,a8,24\n"
 "ee.movi.32.a q3,a9,0\n"
 "and a9,a9,a14\n"
 "srli a9,a9,7\n"
 "mull a9,a9,a15\n"
 "srli a9,a9,25\n"
 "or a8,a8,a9\n"
 "mov a11,a8\n"
 "ee.movi.32.a q1,a8,1\n"
 "and a8,a8,a14\n"
 "srli a8,a8,7\n"
 "mull a8,a8,a15\n"
 "srli a8,a8,24\n"
 "ee.movi.32.a q3,a9,1\n"
 "and a9,a9,a14\n"
 "srli a9,a9,7\n"
 "mull a9,a9,a15\n"
 "srli a9,a9,25\n"
 "or a8,a8,a9\n"
 "slli a8,a8,8\n"
 "or a11,a11,a8\n"
 "ee.movi.32.a q1,a8,2\n"
 "and a8,a8,a14\n"
 "srli a8,a8,7\n"
 "mull a8,a8,a15\n"
 "srli a8,a8,24\n"
 "ee.movi.32.a q3,a9,2\n"
 "and a9,a9,a14\n"
 "srli a9,a9,7\n"
 "mull a9,a9,a15\n"
 "srli a9,a9,25\n"
 "or a8,a8,a9\n"
 "slli a8,a8,16\n"
 "or a11,a11,a8\n"
 "ee.movi.32.a q1,a8,3\n"
 "and a8,a8,a14\n"
 "srli a8,a8,7\n"
 "mull a8,a8,a15\n"
 "srli a8,a8,24\n"
 "ee.movi.32.a q3,a9,3\n"
 "and a9,a9,a14\n"
 "srli a9,a9,7\n"
 "mull a9,a9,a15\n"
 "srli a9,a9,25\n"
 "or a8,a8,a9\n"
 "slli a8,a8,24\n"
 "or a11,a11,a8\n"
 "s32i a10,a4,0\n"
 "s32i a11,a5,0\n"
 "addi a4,a4,4\n"
 "addi a5,a5,4\n"
 "addi a7,a7,-1\n"
 "bnez a7,1b\n"
 "l32i a8,a1,0\n"
 "wsr a8,sar\n"
 "retw\n"
 ".size ht_pack_simd_span_s3,.-ht_pack_simd_span_s3\n"
);
#else
#define HT_SIMD_S3_COMPILED 0
#endif
/* Executable portable model of the register algorithm for host/reference
 * checks. Its timing is NOT S3 SIMD performance. */
static uint8_t ht_simd_mean(uint8_t a,uint8_t b) {
    unsigned x=a^b;
    int half=(int)((x>>1)|(x&128u));
    if(half&128) half-=256;
    int both=a&b;if(both&128) both-=256;
    return (uint8_t)(both+half);
}
static void ht_pack_simd_span_c(const uint8_t *row,const uint8_t *next,
        uint8_t *top,uint8_t *bottom,const uint8_t *constants,unsigned blocks) {
    for(unsigned i=0;i<blocks*16u;++i) {
        uint8_t a=row[i]^128,b=row[i+1]^128,c=next[i]^128,d=next[i+1]^128;
        uint8_t h=ht_simd_mean(a,b),v=ht_simd_mean(a,c);
        uint8_t diagonal=ht_simd_mean(h,ht_simd_mean(c,d));
        int lane=i&15,shift=6-2*(i&3);
        unsigned t=((int8_t)a>(int8_t)constants[32+lane])*2u+
                   ((int8_t)h>(int8_t)constants[48+lane]);
        unsigned u=((int8_t)v>(int8_t)constants[64+lane])*2u+
                   ((int8_t)diagonal>(int8_t)constants[80+lane]);
        if(!(i&3))top[i/4]=bottom[i/4]=0;
        top[i/4]|=(uint8_t)(t<<shift);bottom[i/4]|=(uint8_t)(u<<shift);
    }
}
/* Bounded startup check runs the actual S3 code, not its host substitute.
 * 64 patches x 4 phases; tests both single blocks and a two-block loop. */
static bool ht_simd_selftest(void) {
#if HT_SIMD_S3_COMPILED
    ht_simd_reason="NO MATH API";
    if(!ht_math)return false;
    ht_simd_reason="NO S3 FEATURE";
    if(!(ht_math->features&T5_MATH_FEATURE_S3_DSP))return false;
    ht_simd_reason="TABLE ALIGN";
    if(!ht_simd_constants || ((uintptr_t)ht_simd_constants&15u))return false;
    ht_simd_reason="PACK TEST";
    uint8_t a[48] __attribute__((aligned(16))),b[48] __attribute__((aligned(16)));
    uint8_t got[32] __attribute__((aligned(16))),expected[32] __attribute__((aligned(16)));
    for(unsigned pattern=0;pattern<64;++pattern) {
        for(unsigned i=0;i<48;++i) {
            a[i]=(uint8_t)(pattern<16?pattern*17:ht_hash(pattern*97+i));
            b[i]=(uint8_t)(pattern<16?(15-pattern)*17:ht_hash(pattern*173+i));
        }
        for(unsigned phase=0;phase<4;++phase) {
            memset(got,0xa5,sizeof(got));memset(expected,0xa5,sizeof(expected));
            const uint8_t *k=&ht_simd_constants[phase][0][0];
            unsigned blocks=1+(pattern&1);
            ht_pack_simd_span_c(a,b,expected+4,expected+20,k,blocks);
            ht_pack_simd_span_s3(a,b,got+4,got+20,k,blocks);
            if(!ht_simd_test_equal(got,expected,sizeof(got),pattern,phase))return false;
        }
        if((pattern&7)==7)ht_checkpoint();
    }
    ht_simd_reason="READY";return true;
#else
    ht_simd_reason="NOT S3 BUILD";return false;
#endif
}
