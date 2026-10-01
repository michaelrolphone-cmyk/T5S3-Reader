#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define T5_MATH_API_VERSION 1u
#define T5_MATH_MAX_VECTOR 2048u
#define T5_MATH_MAX_BYTES 4096u
#define T5_MATH_MAX_TRANSFORMS 64u
#define T5_MATH_FEATURE_S3_DSP (1u << 0)
/* CPU-only operations, owning app task, synchronous, no allocation/retention.
 * Buffers are ordinary CPU-addressable RAM, never MMIO. Calls are bounded;
 * callers chunk larger work and poll/yield between chunks.
 * s16 inputs/outputs must be naturally aligned. Exact in-place dst==a or b
 * is supported, partial output/input overlap is rejected. Each mathematical
 * result MUST fit int16_t; overflow behavior is unspecified. No hidden shift,
 * rounding or normalization. A portable scalar fallback handles SIMD tails
 * and non-vector-aligned arrays. False means invalid call; no partial output.
 * Copy rejects overlap (except identical pointers); fill accepts any byte
 * alignment. Zero-count operations accept null buffers. */
typedef struct {
    uint32_t api_version, struct_size, features;
    bool (*add_s16)(const int16_t *a,const int16_t *b,int16_t *dst,size_t count);
    bool (*sub_s16)(const int16_t *a,const int16_t *b,int16_t *dst,size_t count);
    bool (*copy_bytes)(void *dst,const void *src,size_t count);
    bool (*fill_bytes)(void *dst,uint8_t value,size_t count);
    /* Additive 1.3.29 extension; check struct_size before reading these fields.
     * mul: exact signed product, caller guarantees int16 range, no scaling.
     * mat4: row-major C[count][4] = A[count][4] * B[4][4], count <= 64.
     * dot: sum a[i]*b[i], count <= 2048. Float results can differ in rounding
     * from scalar evaluation; inputs must be finite with finite intermediates.
     * Float buffers are naturally aligned; outputs cannot overlap inputs.
     * A zero-length dot writes zero to a valid result; mat4 does no work. */
    bool (*mul_s16)(const int16_t *a,const int16_t *b,int16_t *dst,size_t count);
    bool (*mat4_f32)(const float *points,const float *matrix,float *dst,size_t count);
    bool (*dot_f32)(const float *a,const float *b,float *result,size_t count);
    /* Independent four-component dot products: result[row], count <= 64.
     * Same finite-value/alignment/no-output-overlap rules as float operations. */
    bool (*dot4_f32)(const float *a,const float *b,float *result,size_t count);
} t5_math_api_v1;
const t5_math_api_v1 *t5_math_get_api(uint32_t version);
#ifdef __cplusplus
}
#endif
