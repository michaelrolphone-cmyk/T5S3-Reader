/* Exact memoization of the existing pure 9-byte patch -> 3 residual model.
 * 256 direct-mapped entries, 4 KB. Full patch comparison prevents hash
 * collisions from changing pixels; misses run unchanged inference. No training,
 * approximation, scene/camera key, allocation or frame-sized history. */
#ifndef HT_NN_CACHE_BITS
#define HT_NN_CACHE_BITS 8u
#endif
#define HT_NN_CACHE_SLOTS (1u<<HT_NN_CACHE_BITS)
typedef struct {uint8_t patch[HT_NN_INPUTS],valid;int16_t residual[HT_NN_OUTPUTS];} ht_nn_cache_entry;
static ht_nn_cache_entry ht_nn_cache[HT_NN_CACHE_SLOTS];
static uint32_t ht_nn_cache_lookups,ht_nn_cache_hits;
static void ht_nn_cache_reset(void) {
    memset(ht_nn_cache,0,sizeof(ht_nn_cache));ht_nn_cache_lookups=ht_nn_cache_hits=0;
}
static unsigned ht_nn_cache_slot(const uint8_t *p) {
    uint32_t a=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    uint32_t b=(uint32_t)p[4]|((uint32_t)p[5]<<8)|((uint32_t)p[6]<<16)|((uint32_t)p[7]<<24);
    uint32_t key=(a^((b<<13)|(b>>19))^p[8])*0x9e3779b9u;
    return key>>(32-HT_NN_CACHE_BITS);
}
static void ht_nn_residual_cached_with_workspace(const uint8_t *patch,int16_t *out,bool internal) {
    ht_nn_cache_entry *e=&ht_nn_cache[ht_nn_cache_slot(patch)];++ht_nn_cache_lookups;
    if(e->valid && !memcmp(e->patch,patch,HT_NN_INPUTS)) {
        memcpy(out,e->residual,sizeof(e->residual));++ht_nn_cache_hits;return;
    }
    if(internal)ht_nn_residual_fast(patch,out);
    else ht_nn_residual(patch,out);
    memcpy(e->patch,patch,HT_NN_INPUTS);memcpy(e->residual,out,sizeof(e->residual));e->valid=1;
}

static void ht_nn_residual_cached(const uint8_t *patch,int16_t *out) {
    ht_nn_residual_cached_with_workspace(patch,out,false);
}
