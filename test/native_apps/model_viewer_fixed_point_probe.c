/* Host feasibility probe for Q14 transforms, not an ESP-DSP benchmark.
 * Models Q14 input/coefficient storage and arithmetic output scaling. The
 * thin-face counterexample depends only on input quantization, independent
 * of the SDK's accumulator rounding. Does not enable fixed-point rendering. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include "../../Apps/model_viewer_shading.h"

static int16_t q14(float x) { return (int16_t)(x*16384.0f+(x<0?-0.5f:0.5f)); }
static float quantized(float x) { return q14(x)*(1.0f/16384.0f); }
static uint32_t rng=17;
static float sample(void) { rng=rng*1664525u+1013904223u; return ((int)(rng>>8)-8388608)*(1.0f/16777216.0f); }
int main(void) {
    double max_pixels=0;unsigned tests=0;
    for(unsigned view=0;view<128;++view) {
        float yaw=view*.071f,pitch=view*.113f,cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
        float m[9]={cy,sp*sy,-cp*sy,0,cp,sp,sy,-sp*cy,cp*cy};
        for(unsigned v=0;v<256;++v) {
            float p[3]={sample(),sample(),sample()};
            for(unsigned col=0;col<3;++col) {
                float exact=0;int64_t sum=0;
                for(unsigned k=0;k<3;++k) { exact+=p[k]*m[k*3+col];sum+=(int32_t)q14(p[k])*q14(m[k*3+col]); }
                /* Arithmetic right shift / floor before storing Q14. */
                int64_t scaled=sum>=0?sum/16384:-((-sum+16383)/16384);
                assert(scaled>=-32768 && scaled<=32767);
                double error=fabs(scaled/16384.0-exact)*390*7;
                if(error>max_pixels)max_pixels=error;
                ++tests;
            }
        }
    }
    mv_shade_vertex_t a={0.125f,0.125f,0.125f};
    mv_shade_vertex_t b={0.375f,0.125f,0.125f};
    mv_shade_vertex_t c={0.375f,0.125f+1.0f/65536,0.125f};
    float before[4],after[4];mv_shade_normal(a,b,c,before);
    a=(mv_shade_vertex_t){quantized(a.x),quantized(a.y),quantized(a.z)};
    b=(mv_shade_vertex_t){quantized(b.x),quantized(b.y),quantized(b.z)};
    c=(mv_shade_vertex_t){quantized(c.x),quantized(c.y),quantized(c.z)};
    mv_shade_normal(a,b,c,after);
    assert(before[2]!=0 && after[0]==0 && after[1]==0 && after[2]==0);
    printf("Q14 feasibility: %u coordinate comparisons, max sampled error %.4f pixels at 7x zoom; thin face collapses before multiplication. Retain float production path.\n",tests,max_pixels);
}
