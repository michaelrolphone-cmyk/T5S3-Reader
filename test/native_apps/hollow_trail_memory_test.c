#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../../Apps/hollow_trail_memory.h"

static struct { void *base,*returned; size_t size; } blocks[HT_MEM_BLOCKS];
static unsigned calls,live,fail_at,releases,logs;
static size_t largest_request;
static char last_log[128];
/* A fragmented heap can have sufficient total bytes while rejecting the old
 * combined arena. This is a fault fixture, not a device heap measurement. */
static void *limited_alloc(size_t bytes) {
    ++calls;
    if(bytes>largest_request) largest_request=bytes;
    if(bytes>3100000u || (fail_at && calls==fail_at)) return NULL;
    unsigned i=0;while(i<HT_MEM_BLOCKS && blocks[i].base)++i;
    assert(i<HT_MEM_BLOCKS);
    uint8_t *base=malloc(bytes+33u);assert(base);
    memset(base,0xa5,bytes+33u);
    blocks[i].base=base;blocks[i].returned=base+17u;blocks[i].size=bytes;
    ++live;return blocks[i].returned;
}
static void checked_free(void *pointer) {
    unsigned i=0;while(i<HT_MEM_BLOCKS && blocks[i].returned!=pointer)++i;
    assert(i<HT_MEM_BLOCKS && blocks[i].base);
    const uint8_t *base=blocks[i].base;
    for(unsigned j=0;j<17;++j)assert(base[j]==0xa5);
    for(unsigned j=0;j<16;++j)assert(base[17+blocks[i].size+j]==0xa5);
    free(blocks[i].base);memset(&blocks[i],0,sizeof(blocks[i]));--live;++releases;
}
static void record_log(const char *text) {
    ++logs;snprintf(last_log,sizeof(last_log),"%s",text);
}
static void reset(unsigned failure) {
    assert(!live);calls=releases=logs=0;fail_at=failure;largest_request=0;last_log[0]=0;
}
static void check_buffers(ht_startup_memory *memory) {
    const size_t sizes[]={HT_MEMORY,HT_NATIVE_PIXELS,HT_NATIVE_PIXELS,HT_PACKED_BYTES,HT_PACKED_BYTES};
    for(unsigned i=0;i<HT_MEM_BLOCKS;++i)if(memory->data[i]) {
        assert(((uintptr_t)memory->data[i]&15u)==0);
        memset(memory->data[i],(int)(i+1),sizes[i]);
    }
    /* Every plane retains its exact original capacity and independent contents. */
    for(unsigned i=0;i<HT_MEM_BLOCKS;++i)if(memory->data[i])
        for(size_t j=0;j<sizes[i];++j)assert(memory->data[i][j]==i+1);
}
int main(void) {
    const size_t old_request=HT_MEMORY+HT_NATIVE_MEMORY+HT_PACKED_BYTES+31u;
    reset(0);assert(limited_alloc(old_request)==NULL);assert(live==0);
    ht_startup_memory memory={0};
    reset(0);assert(ht_startup_memory_open(&memory,limited_alloc,checked_free,record_log));
    assert(live==HT_MEM_BLOCKS && calls==HT_MEM_BLOCKS && logs==0);
    assert(largest_request==HT_MEMORY+15u && largest_request<old_request);
    check_buffers(&memory);
    /* Reopening a live set is rejected without leaking or replacing it. */
    assert(!ht_startup_memory_open(&memory,limited_alloc,checked_free,record_log));
    assert(calls==HT_MEM_BLOCKS);
    ht_startup_memory_close(&memory,checked_free);
    ht_startup_memory_close(&memory,checked_free);assert(!live && releases==HT_MEM_BLOCKS);
    for(unsigned stage=1;stage<HT_MEM_BLOCKS;++stage) {
        reset(stage);
        assert(!ht_startup_memory_open(&memory,limited_alloc,checked_free,record_log));
        assert(!live && releases==stage-1 && calls==stage && logs==1);
        assert(strstr(last_log,"bytes"));
        for(unsigned i=0;i<HT_MEM_BLOCKS;++i)assert(!memory.owned[i] && !memory.data[i]);
        /* Retry after partial failure without requiring a new invocation. */
        reset(0);assert(ht_startup_memory_open(&memory,limited_alloc,checked_free,NULL));
        check_buffers(&memory);ht_startup_memory_close(&memory,checked_free);
    }
    reset(HT_MEM_BLOCKS);
    assert(ht_startup_memory_open(&memory,limited_alloc,checked_free,record_log));
    assert(live==HT_MEM_BLOCKS-1 && !memory.data[HT_MEM_STAGING]);
    assert(strstr(last_log,"direct packing"));check_buffers(&memory);
    ht_startup_memory_close(&memory,checked_free);assert(!live);
    printf("Hollow Trail startup memory: PASS (old request %zu, largest new request %u)\n",
           old_request,(unsigned)(HT_MEMORY+15u));
}
