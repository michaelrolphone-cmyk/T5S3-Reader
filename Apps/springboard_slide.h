#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Move cached 2bpp pages without rasterizing icons/fonts during a drag. Offsets
// are rounded to four pixels so landscape movement is also contiguous copies.
static void springboard_compose(uint8_t* out,const uint8_t* current,
    const uint8_t* previous,const uint8_t* next,int width,int height,
    unsigned orientation,int offset) {
    const int stride=width/4;
    const int logical_width=(orientation&1u)?width:height;
    offset=(offset/4)*4;
    if(offset>logical_width) offset=logical_width;
    if(offset < -logical_width) offset=-logical_width;
    memcpy(out,current,(size_t)stride*height);
    if(!offset) return;
    // Stationary top/bottom chrome; only the app grid translates.
    if(!(orientation&1u)) {
        const int left=64/4, bytes=(width-128)/4;
        for(int y=0;y<height;++y) {
            const int lx=orientation==0 ? height-1-y : y;
            int sx=lx-offset;
            const uint8_t* page=current;
            if(sx<0) {sx+=logical_width;page=previous;}
            else if(sx>=logical_width){sx-=logical_width;page=next;}
            const int sy=orientation==0 ? height-1-sx : sx;
            memcpy(out+(size_t)y*stride+left,page+(size_t)sy*stride+left,(size_t)bytes);
        }
    } else {
        // Contiguous physical columns; rotation determines their direction.
        const int shift=(orientation==3 ? offset : -offset)/4;
        for(int y=64;y<height-64;++y) {
            uint8_t* dst=out+(size_t)y*stride;
            const size_t row=(size_t)y*stride;
            if(shift>0) {
                const uint8_t* incoming=orientation==3?previous:next;
                memcpy(dst,incoming+row+stride-shift,(size_t)shift);
                memcpy(dst+shift,current+row,(size_t)(stride-shift));
            } else {
                const int n=-shift;
                const uint8_t* incoming=orientation==3?next:previous;
                memcpy(dst,current+row+n,(size_t)(stride-n));
                memcpy(dst+stride-n,incoming+row,(size_t)n);
            }
        }
    }
}
