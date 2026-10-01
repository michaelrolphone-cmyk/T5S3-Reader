#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint32_t scan,soi,length,nonzero;bool found,done;} cam_jpeg_scan;
/* available contains only immutable, CPU-owned DMA descriptors. At most 512
 * bytes examined per poll, including markers split over descriptor boundaries.
 * A late-start prefix is discarded; incomplete JPEGs are never published. */
static inline void cam_jpeg_scan_step(cam_jpeg_scan *s,const uint8_t *bytes,uint32_t available){
    if(s->done || available<s->scan)return;
    uint32_t end=available-s->scan>512?s->scan+512:available;
    for(;s->scan<end;s->scan++){
        uint32_t p=s->scan;
        if(bytes[p])s->nonzero++;
        if(!s->found && p>=2 && bytes[p-2]==0xff && bytes[p-1]==0xd8 && bytes[p]==0xff){s->soi=p-2;s->found=true;}
        else if(s->found && p>s->soi+2 && bytes[p-1]==0xff && bytes[p]==0xd9){s->length=p+1-s->soi;s->done=true;return;}
    }
}
