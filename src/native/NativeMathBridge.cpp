#include "T5MathApi.h"
#include "T5AppApi.h"
#include <cstring>
#if !defined(NATIVE_MATH_HOST_TEST)
#include "sdkconfig.h"
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
#include "dsps_add.h"
#include "dsps_sub.h"
#include "dsps_mem.h"
#include "dsps_mul.h"
#include "dsps_dotprod.h"
#include "dspm_mult.h"
#define NATIVE_MATH_DSP 1
#else
#define NATIVE_MATH_DSP 0
#endif
namespace {
bool active() { return t5_app_get_api(T5_APP_ABI_VERSION)!=nullptr; }
bool validRange(const void *p,size_t bytes) {
    return p && reinterpret_cast<uintptr_t>(p)<=UINTPTR_MAX-bytes;
}
bool overlaps(const void *a,const void *b,size_t bytes) {
    const uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
    return x<y+bytes && y<x+bytes;
}
bool vector(unsigned operation,const int16_t *a,const int16_t *b,int16_t *dst,size_t count) {
    if(!active() || count>T5_MATH_MAX_VECTOR) return false;
    if(!count) return true;
    size_t bytes=count*sizeof(int16_t);
    if(!validRange(a,bytes) || !validRange(b,bytes) || !validRange(dst,bytes) ||
       ((reinterpret_cast<uintptr_t>(a)|reinterpret_cast<uintptr_t>(b)|reinterpret_cast<uintptr_t>(dst))&1u) ||
       (dst!=a && overlaps(dst,a,bytes)) || (dst!=b && overlaps(dst,b,bytes))) return false;
    size_t done=0;
#if NATIVE_MATH_DSP
    const uintptr_t aa=reinterpret_cast<uintptr_t>(a),bb=reinterpret_cast<uintptr_t>(b),dd=reinterpret_cast<uintptr_t>(dst);
    if((aa&15u)==(bb&15u) && (aa&15u)==(dd&15u)) {
        size_t prefix=((16u-(aa&15u))&15u)/2u;
        if(prefix>count) prefix=count;
        for(;done<prefix;++done) dst[done]=static_cast<int16_t>(operation==2?a[done]*b[done]:operation==1?a[done]-b[done]:a[done]+b[done]);
        // Bundled AES3 kernels preload the next 128-bit input vector, even
        // after the last output vector. Leave >=8 real elements for scalar
        // cleanup; that preload then stays inside the supplied input range.
        // Check output alignment too (the upstream entry only checks inputs).
        size_t middle=count-done>=16u?(count-done-8u)&~size_t(7u):0u;
        if(middle) {
            if(operation==2) (void)dsps_mul_s16_aes3(a+done,b+done,dst+done,static_cast<int>(middle),1,1,1,0);
            else if(operation==1) (void)dsps_sub_s16_aes3(a+done,b+done,dst+done,static_cast<int>(middle),1,1,1,0);
            else (void)dsps_add_s16_aes3(a+done,b+done,dst+done,static_cast<int>(middle),1,1,1,0);
            done+=middle;
        }
    }
#endif
    for(;done<count;++done) dst[done]=static_cast<int16_t>(operation==2?a[done]*b[done]:operation==1?a[done]-b[done]:a[done]+b[done]);
    return true;
}
bool add(const int16_t *a,const int16_t *b,int16_t *d,size_t n) { return vector(false,a,b,d,n); }
bool sub(const int16_t *a,const int16_t *b,int16_t *d,size_t n) { return vector(true,a,b,d,n); }
bool copy(void *dst,const void *src,size_t count) {
    if(!active() || count>T5_MATH_MAX_BYTES) return false;
    if(!count) return true;
    if(!validRange(dst,count) || !validRange(src,count)) return false;
    if(dst==src) return true;
    if(overlaps(dst,src,count)) return false;
#if NATIVE_MATH_DSP
    // Avoid the bundled assembly's unaligned read-ahead and wide prefix
    // accesses. Only aligned, whole-vector ranges use the DSP routine.
    if(!((reinterpret_cast<uintptr_t>(dst)|reinterpret_cast<uintptr_t>(src)|count)&15u)) {
        dsps_memcpy_aes3(dst,src,count); return true;
    }
#endif
    std::memcpy(dst,src,count); return true;
}
bool fill(void *dst,uint8_t value,size_t count) {
    if(!active() || count>T5_MATH_MAX_BYTES) return false;
    if(!count) return true;
    if(!validRange(dst,count)) return false;
#if NATIVE_MATH_DSP
    if(!((reinterpret_cast<uintptr_t>(dst)|count)&15u)) {
        dsps_memset_aes3(dst,value,count); return true;
    }
#endif
    std::memset(dst,value,count); return true;
}
bool mul(const int16_t *a,const int16_t *b,int16_t *d,size_t n) { return vector(2,a,b,d,n); }
bool floatRange(const float *p,size_t count) {
    return validRange(p,count*sizeof(float)) && !(reinterpret_cast<uintptr_t>(p)&3u);
}
bool rangesOverlap(const void *a,size_t as,const void *b,size_t bs) {
    auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
    return x<y+bs && y<x+as;
}
bool mat4(const float *points,const float *matrix,float *dst,size_t count) {
    if(!active() || count>T5_MATH_MAX_TRANSFORMS) return false;
    if(!count) return true;
    size_t bytes=count*4*sizeof(float);
    if(!floatRange(points,count*4) || !floatRange(matrix,16) || !floatRange(dst,count*4) ||
       rangesOverlap(dst,bytes,points,bytes) || rangesOverlap(dst,bytes,matrix,16*sizeof(float))) return false;
#if NATIVE_MATH_DSP
    if(!(count&3u) && !((reinterpret_cast<uintptr_t>(points)|reinterpret_cast<uintptr_t>(matrix)|reinterpret_cast<uintptr_t>(dst))&15u)) {
        (void)dspm_mult_f32_aes3(points,matrix,dst,static_cast<int>(count),4,4); return true;
    }
#endif
    for(size_t row=0;row<count;++row) for(size_t col=0;col<4;++col) {
        float value=points[row*4]*matrix[col];
        for(size_t k=1;k<4;++k) value+=points[row*4+k]*matrix[k*4+col];
        dst[row*4+col]=value;
    }
    return true;
}
bool dot(const float *a,const float *b,float *out,size_t count) {
    if(!active() || count>T5_MATH_MAX_VECTOR || !floatRange(out,1)) return false;
    if(!count) { *out=0; return true; }
    const size_t bytes=count*sizeof(float);
    if(!floatRange(a,count) || !floatRange(b,count) ||
       rangesOverlap(out,sizeof(float),a,bytes) || rangesOverlap(out,sizeof(float),b,bytes)) return false;
    float value=0; size_t done=0;
#if NATIVE_MATH_DSP
    if(!((reinterpret_cast<uintptr_t>(a)|reinterpret_cast<uintptr_t>(b))&15u)) {
        done=count&~size_t(3u);
        if(done) (void)dsps_dotprod_f32_aes3(a,b,&value,static_cast<int>(done));
    }
#endif
    for(;done<count;++done) value+=a[done]*b[done];
    *out=value; return true;
}
bool dot4(const float *a,const float *b,float *out,size_t count) {
    if(!active() || count>T5_MATH_MAX_TRANSFORMS) return false;
    if(!count) return true;
    size_t bytes=count*4*sizeof(float),out_bytes=count*sizeof(float);
    if(!floatRange(a,count*4) || !floatRange(b,count*4) || !floatRange(out,count) ||
       rangesOverlap(out,out_bytes,a,bytes) || rangesOverlap(out,out_bytes,b,bytes)) return false;
    for(size_t i=0;i<count;++i) {
#if NATIVE_MATH_DSP
        if(!((reinterpret_cast<uintptr_t>(a)|reinterpret_cast<uintptr_t>(b))&15u)) {
            (void)dsps_dotprod_f32_aes3(a+i*4,b+i*4,out+i,4); continue;
        }
#endif
        float v=0; for(size_t k=0;k<4;++k) v+=a[i*4+k]*b[i*4+k]; out[i]=v;
    }
    return true;
}
const t5_math_api_v1 api={T5_MATH_API_VERSION,sizeof(t5_math_api_v1),
    NATIVE_MATH_DSP?T5_MATH_FEATURE_S3_DSP:0u,add,sub,copy,fill,mul,mat4,dot,dot4};
}
extern "C" const t5_math_api_v1 *t5_math_get_api(uint32_t version) {
    return version==T5_MATH_API_VERSION && active()?&api:nullptr;
}
