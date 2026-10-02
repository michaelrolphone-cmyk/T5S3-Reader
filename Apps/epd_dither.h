#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Fixed physical-screen phase; no frame/camera/time dependency. Input is
 * darkness: 0 white, 255 black. Endpoints remain solid. */
static const uint8_t epd_dither_rank[8][8] = {
    {0,48,12,60,3,51,15,63}, {32,16,44,28,35,19,47,31},
    {8,56,4,52,11,59,7,55}, {40,24,36,20,43,27,39,23},
    {2,50,14,62,1,49,13,61}, {34,18,46,30,33,17,45,29},
    {10,58,6,54,9,57,5,53}, {42,26,38,22,41,25,37,21}
};
static inline bool epd_dither_black(uint8_t darkness, unsigned x, unsigned y) {
    return darkness > (unsigned)epd_dither_rank[y & 7u][x & 7u] * 4u + 2u;
}
