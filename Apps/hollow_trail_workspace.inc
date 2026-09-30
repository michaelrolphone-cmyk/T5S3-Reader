/* A single optional allocation, reused synchronously by sequential rendering stages.
 * Bulk rasters stay in PSRAM. The app's tracked heap_caps_malloc import uses
 * the ESP-IDF compatibility ABI: MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT.
 * No fallback allocation: a failed request leaves the baseline available. */
#define HT_FAST_BYTES 4096u
#define HT_FAST_ALLOC_BYTES (HT_FAST_BYTES+15u)
#define HT_INTERNAL_8BIT_CAPS ((1u<<11)|(1u<<2))
typedef union {
    struct {
        int8_t w1[HT_NN_HIDDEN][HT_NN_INPUTS],w2[HT_NN_OUTPUTS][HT_NN_HIDDEN];
        int32_t b1[HT_NN_HIDDEN],b2[HT_NN_OUTPUTS];
        int hidden[HT_NN_HIDDEN];
        uint8_t rows[3][HT_RECON_W],out[2][HT_SCENE_W];
    } neural;
    struct {
        uint8_t source[HT_W];
        int16_t add[HT_W],sub[HT_W];
        uint8_t out[HT_W];
    } blur;
    struct {
        uint8_t constants[4][6][16];
        uint8_t rows[2][HT_W];
        uint8_t out[2][HT_W/4];
    } pack;
} ht_fast_workspace;
typedef char ht_workspace_size_check[sizeof(ht_fast_workspace)<=HT_FAST_BYTES?1:-1];
typedef char ht_workspace_row_alignment_check[HT_W%16==0?1:-1];
static ht_fast_workspace *ht_fast;
static void ht_workspace_attach(void *raw) {
    ht_fast=raw?(ht_fast_workspace *)(((uintptr_t)raw+15u)&~(uintptr_t)15u):NULL;
}
static void *ht_workspace_open(void *(*allocate)(size_t,uint32_t)) {
    void *raw=allocate?allocate(HT_FAST_ALLOC_BYTES,HT_INTERNAL_8BIT_CAPS):NULL;
    ht_workspace_attach(raw);return raw;
}
static void ht_nn_fast_init(void) {
    memcpy(ht_fast->neural.w1,ht_nn_w1,sizeof(ht_nn_w1));
    memcpy(ht_fast->neural.w2,ht_nn_w2,sizeof(ht_nn_w2));
    memcpy(ht_fast->neural.b1,ht_nn_b1,sizeof(ht_nn_b1));
    memcpy(ht_fast->neural.b2,ht_nn_b2,sizeof(ht_nn_b2));
}
static inline __attribute__((always_inline)) void ht_nn_residual_fast(const uint8_t *patch,int16_t *out) {
    int *hidden=ht_fast->neural.hidden;
    for(int h=0;h<HT_NN_HIDDEN;++h) {
        int acc=ht_fast->neural.b1[h];
        for(int i=0;i<HT_NN_INPUTS;++i)acc+=((int)patch[i]-128)*(int)ht_fast->neural.w1[h][i];
        int q=(acc+16)>>5;hidden[h]=q>0?q:0;
    }
    for(int o=0;o<HT_NN_OUTPUTS;++o) {
        int acc=ht_fast->neural.b2[o];
        for(int h=0;h<HT_NN_HIDDEN;++h)acc+=hidden[h]*(int)ht_fast->neural.w2[o][h];
        int q=ht_nn_round_shift(acc,9);
        out[o]=(int16_t)(q<-127?-127:q>127?127:q);
    }
}
