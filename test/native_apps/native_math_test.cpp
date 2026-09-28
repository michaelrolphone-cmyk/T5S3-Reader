#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#define NATIVE_MATH_HOST_TEST 1
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "../../src/native/NativeMathBridge.cpp"
#include "../../Apps/hollow_trail_engine.inc"
#include "../../Apps/model_viewer_shading.h"
static bool owner=true;
static unsigned vector_calls,copy_calls,fill_calls,mul_calls,dot_calls,matrix_calls;
static const int16_t *input_end;
extern "C" const t5_app_api_v1 *t5_app_get_api(uint32_t version) {
    static t5_app_api_v1 a={}; return owner && version==1?&a:nullptr;
}
static int dsp_model(bool subtract,const int16_t *a,const int16_t *b,int16_t *out,int n,int sa,int sb,int so,int shift) {
    assert(n>=8 && !(n&7) && sa==1 && sb==1 && so==1 && shift==0);
    assert(!((reinterpret_cast<uintptr_t>(a)|reinterpret_cast<uintptr_t>(b)|reinterpret_cast<uintptr_t>(out))&15u));
    if(input_end) assert(a+n+8<=input_end);
    // Simulate the real bundled assembly's final 128-bit read-ahead under ASan.
    volatile int16_t read_ahead=a[n+7]; (void)read_ahead;
    for(int i=0;i<n;++i) out[i]=static_cast<int16_t>(subtract?a[i]-b[i]:a[i]+b[i]);
    ++vector_calls; return 0;
}
int dsps_add_s16_aes3(const int16_t *a,const int16_t *b,int16_t *d,int n,int sa,int sb,int so,int sh) { return dsp_model(false,a,b,d,n,sa,sb,so,sh); }
int dsps_sub_s16_aes3(const int16_t *a,const int16_t *b,int16_t *d,int n,int sa,int sb,int so,int sh) { return dsp_model(true,a,b,d,n,sa,sb,so,sh); }
void *dsps_memcpy_aes3(void *d,const void *s,size_t n) {
    assert(!((reinterpret_cast<uintptr_t>(d)|reinterpret_cast<uintptr_t>(s)|n)&15u));
    ++copy_calls; return std::memcpy(d,s,n);
}
void *dsps_memset_aes3(void *d,uint8_t v,size_t n) {
    assert(!((reinterpret_cast<uintptr_t>(d)|n)&15u)); ++fill_calls; return std::memset(d,v,n);
}
int dsps_mul_s16_aes3(const int16_t *a,const int16_t *b,int16_t *d,int n,int sa,int sb,int so,int sh) {
    assert(n>=8 && !(n&7) && sa==1 && sb==1 && so==1 && sh==0);
    assert(!((uintptr_t(a)|uintptr_t(b)|uintptr_t(d))&15u));
    if(input_end) assert(a+n+8<=input_end);
    volatile int16_t preload=a[n+7]; (void)preload;
    for(int i=0;i<n;++i) d[i]=(int16_t)(a[i]*b[i]);
    ++mul_calls; return 0;
}
int dsps_dotprod_f32_aes3(const float *a,const float *b,float *d,int n) {
    assert(n>0 && !(n&3) && !((uintptr_t(a)|uintptr_t(b))&15u));
    float partial[4]={};
    for(int i=0;i<n;i+=4) for(int k=0;k<4;++k) partial[k]+=a[i+k]*b[i+k];
    *d=((partial[0]+partial[1])+partial[2])+partial[3]; ++dot_calls; return 0;
}
int dspm_mult_f32_aes3(const float *a,const float *b,float *d,int m,int n,int k) {
    assert(m>0 && !(m&3) && n==4 && k==4);
    assert(!((uintptr_t(a)|uintptr_t(b)|uintptr_t(d))&15u));
    for(int r=0;r<m;++r) for(int c=0;c<k;++c) {
        float v=a[r*4]*b[c]; for(int x=1;x<4;++x) v+=a[r*4+x]*b[x*4+c]; d[r*4+c]=v;
    }
    ++matrix_calls; return 0;
}
static void extensions(const t5_math_api_v1 *m) {
    alignas(16) int16_t a[160],b[160],d[160];
    for(int off=0;off<8;++off) for(int n=0;n<128;++n) for(int mismatch=0;mismatch<2;++mismatch) {
        for(int i=0;i<160;++i) {a[i]=(int16_t)(i-80); b[i]=(int16_t)(127-i); d[i]=12345;}
        input_end=a+off+n;
        assert(m->mul_s16(a+off,b+off,d+off+mismatch,n));
        for(int i=0;i<n;++i) assert(d[off+mismatch+i]==a[off+i]*b[off+i]);
        assert(d[off+mismatch+n]==12345);
        assert(m->mul_s16(a+off,b+off,a+off,n));
        for(int i=0;i<n;++i) assert(a[off+i]==(off+i-80)*(127-off-i));
    }
    input_end=nullptr;
    assert(!m->mul_s16(a,b,a+1,12)); assert(!m->mul_s16(a,b,d,2049));
    alignas(16) float points[264],matrix[20],out[264],bfloat[264];
    for(int i=0;i<264;++i) {points[i]=(float)(i%7-3); bfloat[i]=(float)(i%5-2);}
    for(int i=0;i<20;++i) matrix[i]=(float)(i%3-1);
    for(int off=0;off<4;++off) for(int n=1;n<=64;++n) {
        out[n*4+off]=999;
        assert(m->mat4_f32(points+off,matrix+off,out+off,n));
        for(int r=0;r<n;++r) for(int c=0;c<4;++c) {
            float v=0; for(int k=0;k<4;++k) v+=points[off+r*4+k]*matrix[off+k*4+c];
            assert(out[off+r*4+c]==v);
        }
        assert(out[n*4+off]==999);
        float value=999,expected=0;
        assert(m->dot_f32(points+off,bfloat+off,&value,n));
        for(int i=0;i<n;++i) expected+=points[off+i]*bfloat[off+i];
        assert(value==expected);
        assert(m->dot4_f32(points+off,bfloat+off,out,n));
        for(int i=0;i<n;++i) {
            expected=0; for(int k=0;k<4;++k) expected+=points[off+4*i+k]*bfloat[off+4*i+k];
            assert(out[i]==expected);
        }
    }
    float value=123;
    assert(m->dot_f32(nullptr,nullptr,&value,0) && value==0);
    assert(m->mat4_f32(nullptr,nullptr,nullptr,0));
    assert(!m->mat4_f32(points,matrix,points,4));
    assert(!m->mat4_f32(points,matrix,out,65));
    assert(!m->dot_f32(points,bfloat,points+1,4));
    assert(!m->dot4_f32(points,bfloat,points+1,4));
    assert(!m->dot4_f32(points,bfloat,out,65));
    owner=false;
    assert(!m->mul_s16(a,b,d,1) && !m->mat4_f32(points,matrix,out,4));
    assert(!m->dot_f32(points,bfloat,&value,4) && !m->dot4_f32(points,bfloat,out,4)); owner=true;
    alignas(16) float normals[32],lights[32]; float lengths[8],illumination[8];
    uint8_t expected_density[8];
    for(int i=0;i<8;++i) {
        mv_shade_vertex_t a={0,0,0},b={(float)i,1,2},c={2,(float)(i-3),1};
        mv_shade_normal(a,b,c,normals+i*4);
        lights[i*4]=-0.45f; lights[i*4+1]=0.65f; lights[i*4+2]=0.61237244f; lights[i*4+3]=0;
        expected_density[i]=mv_shade_density(a,b,c);
    }
    assert(m->dot4_f32(normals,normals,lengths,8));
    assert(m->dot4_f32(normals,lights,illumination,8));
    for(int i=0;i<8;++i) assert(mv_shade_normal_density(normals+i*4,lengths[i],illumination[i])==expected_density[i]);
    assert(mul_calls && matrix_calls && dot_calls);
}
int main() {
    const auto *m=t5_math_get_api(1); assert(m && !t5_math_get_api(2));
    extensions(m);
    alignas(16) int16_t a[2064],b[2064],d[2064];
    for(int offset=0;offset<8;++offset) for(int n=0;n<=127;++n) for(int mismatch=0;mismatch<2;++mismatch) {
        for(int i=0;i<2064;++i) { a[i]=static_cast<int16_t>(i*3-10000); b[i]=static_cast<int16_t>(i-2000); d[i]=12345; }
        input_end=a+offset+n;
        assert(m->add_s16(a+offset,b+offset,d+offset+mismatch,n));
        for(int i=0;i<n;++i) assert(d[offset+mismatch+i]==a[offset+i]+b[offset+i]);
        assert(d[offset+mismatch+n]==12345);
        assert(m->sub_s16(a+offset,b+offset,a+offset,n));
        for(int i=0;i<n;++i) assert(a[offset+i]==(offset+i)*2-8000);
    }
    input_end=nullptr;
    assert(vector_calls>0);
    assert(!m->add_s16(a,b,d,T5_MATH_MAX_VECTOR+1));
    assert(!m->sub_s16(a,b,a+1,16));
    assert(!m->add_s16(nullptr,b,d,1));
    assert(m->add_s16(nullptr,nullptr,nullptr,0));
    alignas(16) uint8_t src[256],dst[256];
    memset(src,91,sizeof(src));
    for(size_t offset=0;offset<16;++offset) for(size_t n=0;n<128;++n) {
        memset(dst,37,sizeof(dst));
        assert(m->copy_bytes(dst+offset,src+offset,n));
        assert(!memcmp(dst+offset,src+offset,n) && dst[offset+n]==37);
        assert(m->fill_bytes(dst+offset,142,n));
        for(size_t i=0;i<n;++i) assert(dst[offset+i]==142);
        assert(dst[offset+n]==37);
    }
    assert(copy_calls && fill_calls);
    assert(!m->copy_bytes(src+1,src,32));
    assert(!m->fill_bytes(dst,0,T5_MATH_MAX_BYTES+1));
    owner=false; assert(!t5_math_get_api(1) && !m->fill_bytes(dst,0,1)); owner=true;
    // The actual renderer consumes the ABI and preserves the scalar pixels.
    uint8_t *memory=static_cast<uint8_t *>(malloc(HT_MEMORY));
    uint8_t *expected=static_cast<uint8_t *>(malloc(HT_PIXELS));
    assert(memory && expected); ht_bind(memory); ht_spawn(true);
    for(int i=0;i<4;++i) {
        ht.camera=i*711*256; ht.x=(i*711+165)*256;
        ht_math=nullptr; memset(ht_cache_valid,0,sizeof(ht_cache_valid));
        ht_render_scene(); memcpy(expected,ht_scene,HT_PIXELS);
        ht_math=m; memset(ht_cache_valid,0,sizeof(ht_cache_valid)); ht_render_scene();
        assert(!memcmp(expected,ht_scene,HT_PIXELS));
    }
    free(expected); free(memory);
    puts("Math API: bounds, alignment, read-ahead, in-place/tails, owner checks and renderer equivalence PASS");
}
