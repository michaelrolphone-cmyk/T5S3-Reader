#ifndef HOLLOW_TRAIL_MEMORY_H
#define HOLLOW_TRAIL_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Keep the existing scenery/SIMD arena layout. Native raster planes, journal
 * and optional packed staging do not need to be adjacent to that arena.
 * Allocate largest-first, once per launch; never allocate while rendering. */
enum { HT_MEM_SCENERY, HT_MEM_NATIVE_A, HT_MEM_NATIVE_B, HT_MEM_READER,
       HT_MEM_STAGING, HT_MEM_BLOCKS };
typedef struct {
    void *owned[HT_MEM_BLOCKS];
    uint8_t *data[HT_MEM_BLOCKS];
} ht_startup_memory;

static void ht_startup_memory_close(ht_startup_memory *memory, void (*release)(void *)) {
    for(unsigned i=HT_MEM_BLOCKS;i>0;--i) {
        if(memory->owned[i-1]) release(memory->owned[i-1]);
        memory->owned[i-1]=NULL;
        memory->data[i-1]=NULL;
    }
}

static bool ht_startup_memory_open(ht_startup_memory *memory,
                                  void *(*allocate)(size_t), void (*release)(void *),
                                  void (*log_message)(const char *)) {
    const size_t sizes[HT_MEM_BLOCKS]={HT_MEMORY,HT_NATIVE_PIXELS,HT_NATIVE_PIXELS,
                                     HT_PACKED_BYTES,HT_PACKED_BYTES};
    const char *const names[HT_MEM_BLOCKS]={"scenery", "native A", "native B", "journal", "staging"};
    for(unsigned i=0;i<HT_MEM_BLOCKS;++i) if(memory->owned[i]) return false;
    for(unsigned i=0;i<HT_MEM_BLOCKS;++i) {
        /* Preserve SIMD/packed-row alignment even with an unaligned allocator. */
        memory->owned[i]=allocate(sizes[i]+15u);
        if(!memory->owned[i]) {
            if(log_message) {
                char message[128];
                snprintf(message,sizeof(message),"Hollow Trail: %s PSRAM allocation failed (%lu bytes)%s",
                         names[i],(unsigned long)(sizes[i]+15u),
                         i==HT_MEM_STAGING?"; using direct packing":"");
                log_message(message);
            }
            if(i==HT_MEM_STAGING) return true;
            ht_startup_memory_close(memory,release);
            return false;
        }
        memory->data[i]=(uint8_t *)(((uintptr_t)memory->owned[i]+15u)&~(uintptr_t)15u);
    }
    return true;
}
#endif
