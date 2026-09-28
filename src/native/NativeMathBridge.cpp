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
bool vector(bool subtract,const int16_t *a,const int16_t *b,int16_t *dst,size_t count) {
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
        for(;done<prefix;++done) dst[done]=static_cast<int16_t>(subtract?a[done]-b[done]:a[done]+b[done]);
        // Bundled AES3 kernels preload the next 128-bit input vector, even
        // after the last output vector. Leave >=8 real elements for scalar
        // cleanup; that preload then stays inside the supplied input range.
        // Check output alignment too (the upstream entry only checks inputs).
        size_t middle=count-done>=16u?(count-done-8u)&~size_t(7u):0u;
        if(middle) {
            if(subtract) (void)dsps_sub_s16_aes3(a+done,b+done,dst+done,static_cast<int>(middle),1,1,1,0);
            else (void)dsps_add_s16_aes3(a+done,b+done,dst+done,static_cast<int>(middle),1,1,1,0);
            done+=middle;
        }
    }
#endif
    for(;done<count;++done) dst[done]=static_cast<int16_t>(subtract?a[done]-b[done]:a[done]+b[done]);
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
const t5_math_api_v1 api={T5_MATH_API_VERSION,sizeof(t5_math_api_v1),
    NATIVE_MATH_DSP?T5_MATH_FEATURE_S3_DSP:0u,add,sub,copy,fill};
}
extern "C" const t5_math_api_v1 *t5_math_get_api(uint32_t version) {
    return version==T5_MATH_API_VERSION && active()?&api:nullptr;
}
