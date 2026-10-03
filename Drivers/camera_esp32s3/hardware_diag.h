#pragma once
#include <stdint.h>
/* Five fixed register snapshots, <=60 bytes plus NUL. phase is one of the
 * internal constant tags VSYNC/DMA/JPEG, defensively bounded to five bytes. */
static inline void cam_hw_format_detail(char out[64],const char *phase,
    uint32_t ctrl,uint32_t raw,uint32_t dma,uint32_t gpio,uint32_t changes) {
    unsigned at=0;
    while(at<5 && phase[at]){out[at]=phase[at];at++;}
    const uint32_t values[5]={ctrl,raw,dma,gpio,changes};
    const char labels[5]={'c','r','d','g','x'};
    const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<5;i++){
        out[at++]=' ';out[at++]=labels[i];out[at++]='=';
        for(unsigned n=0;n<8;n++)out[at++]=hex[(values[i]>>(28-4*n))&15];
    }
    out[at]=0;
}
