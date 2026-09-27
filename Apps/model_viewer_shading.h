#ifndef MODEL_VIEWER_SHADING_H
#define MODEL_VIEWER_SHADING_H

/* App-owned flat shading. No panel, firmware or driver APIs: the caller supplies
 * a logical viewport, scratch depth storage and its normal pixel writer. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct { float x, y, z; } mv_shade_vertex_t;
typedef void (*mv_shade_pixel_fn)(uint8_t *, int, int, bool);
typedef bool (*mv_shade_service_fn)(void);
typedef struct {
    uint16_t *depth;
    int left, top, width, height, sample, cols, rows;
    mv_shade_service_fn service;
} mv_shade_surface_t;

static float mv_shade_abs(float x) { return x < 0.0f ? -x : x; }
static float mv_shade_min(float a, float b) { return a < b ? a : b; }
static float mv_shade_max(float a, float b) { return a > b ? a : b; }
static bool mv_shade_finite(float x) { return x == x && x > -1.0e20f && x < 1.0e20f; }

/* Avoid adding libm/compiler division imports to the native ELF ABI. */
static float mv_shade_recip(float x) {
    uint32_t bits;
    float r;
    memcpy(&bits, &x, sizeof(bits));
    bits = 0x7ef311c3u - bits;
    memcpy(&r, &bits, sizeof(r));
    r *= 2.0f - x * r;
    r *= 2.0f - x * r;
    r *= 2.0f - x * r;
    return r;
}

/* Two-sided neutral material, lit from above/left of the camera. Derive the
 * normal from geometry, not optional/untrusted OBJ/STL normal records. Flipping
 * winding must not make a surface vanish or change its apparent material. */
static uint8_t mv_shade_density(mv_shade_vertex_t a, mv_shade_vertex_t b,
                                mv_shade_vertex_t c) {
    const float ux = b.x-a.x, uy = b.y-a.y, uz = b.z-a.z;
    const float vx = c.x-a.x, vy = c.y-a.y, vz = c.z-a.z;
    float nx = uy*vz-uz*vy, ny = uz*vx-ux*vz, nz = ux*vy-uy*vx;
    const float largest = mv_shade_max(mv_shade_abs(nx),
                           mv_shade_max(mv_shade_abs(ny), mv_shade_abs(nz)));
    if (!mv_shade_finite(largest) || largest < 1.0e-20f) return 9u;
    const float rescale = mv_shade_recip(largest);
    nx *= rescale; ny *= rescale; nz *= rescale;
    const float length2 = nx*nx + ny*ny + nz*nz;
    uint32_t bits;
    float inv_length;
    memcpy(&bits, &length2, sizeof(bits));
    bits = 0x5f3759dfu - (bits >> 1);
    memcpy(&inv_length, &bits, sizeof(inv_length));
    inv_length *= 1.5f - 0.5f*length2*inv_length*inv_length;
    inv_length *= 1.5f - 0.5f*length2*inv_length*inv_length;
    if (nz < 0.0f) inv_length = -inv_length;
    const float diffuse = mv_shade_max(0.0f, mv_shade_min(1.0f,
                          (-0.45f*nx + 0.65f*ny + 0.61237244f*nz)*inv_length));
    /* 2..14 black samples of 16: highlights remain visible against white,
     * while ambient light prevents shadowed faces from becoming solid black. */
    return (uint8_t)(14.0f - 12.0f*diffuse + 0.5f);
}

static bool mv_shade_black(int x, int y, uint8_t density) {
    static const uint8_t bayer[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    return bayer[((unsigned)y & 3u)*4u + ((unsigned)x & 3u)] < density;
}

static bool mv_shade_begin(mv_shade_surface_t *s, uint16_t *depth, size_t capacity,
                           int left, int top, int width, int height, int sample,
                           mv_shade_service_fn service) {
    if (!s || !depth || width < 1 || height < 1 || width > 4096 || height > 4096 ||
        left < 0 || top < 0 || left > 4096 || top > 4096 ||
        (sample != 1 && sample != 2)) return false;
    const int cols = (width + sample - 1) / sample;
    const int rows = (height + sample - 1) / sample;
    const size_t count = (size_t)cols * (size_t)rows;
    if (capacity < count) return false;
    *s = (mv_shade_surface_t){depth,left,top,width,height,sample,cols,rows,service};
    memset(depth, 0, count * sizeof(*depth));
    return true;
}

static float mv_shade_edge(mv_shade_vertex_t a, mv_shade_vertex_t b, float x, float y) {
    return (b.x-a.x)*(y-a.y) - (b.y-a.y)*(x-a.x);
}

/* Orthographic depth is affine across the triangle. Larger camera Z is closer.
 * Both black AND white dither samples overwrite the previous pixel on a depth
 * pass, otherwise rear surfaces leak through the white holes of a nearer face.
 * A 2x2 sample grid speeds motion without dropping triangles/opening mesh holes;
 * the idle pass uses one sample per pixel and the same screen-anchored dither. */
static bool mv_shade_triangle(mv_shade_surface_t *s, uint8_t *buffer,
                              mv_shade_pixel_fn pixel, mv_shade_vertex_t a,
                              mv_shade_vertex_t b, mv_shade_vertex_t c,
                              uint8_t density) {
    mv_shade_vertex_t *vertices[3] = {&a,&b,&c};
    if (!s || !s->depth || !buffer || !pixel) return false;
    const float factor = s->sample == 2 ? 0.5f : 1.0f;
    for (int i = 0; i < 3; ++i) {
        mv_shade_vertex_t *v = vertices[i];
        if (!mv_shade_finite(v->x) || !mv_shade_finite(v->y) ||
            !mv_shade_finite(v->z)) return true;
        v->x = (v->x - (float)s->left)*factor;
        v->y = (v->y - (float)s->top)*factor;
        v->z = mv_shade_max(1.0f, mv_shade_min(65534.0f, 32768.0f + v->z*16000.0f));
    }
    const float min_x = mv_shade_min(a.x, mv_shade_min(b.x,c.x));
    const float max_x = mv_shade_max(a.x, mv_shade_max(b.x,c.x));
    const float min_y = mv_shade_min(a.y, mv_shade_min(b.y,c.y));
    const float max_y = mv_shade_max(a.y, mv_shade_max(b.y,c.y));
    if (max_x < 0.0f || max_y < 0.0f || min_x >= s->cols || min_y >= s->rows) return true;
    float area = mv_shade_edge(a,b,c.x,c.y);
    if (!mv_shade_finite(area) || mv_shade_abs(area) < 1.0e-7f) return true;
    if (area < 0.0f) { mv_shade_vertex_t tmp=b; b=c; c=tmp; area=-area; }
    /* Clamp BEFORE converting to integers: off-screen geometry cannot overflow
     * raster bounds or write into the header/footer, even at extreme pan/zoom. */
    const int x0 = (int)mv_shade_max(0.0f,min_x);
    const int x1 = (int)mv_shade_min((float)(s->cols-1),max_x);
    const int y0 = (int)mv_shade_max(0.0f,min_y);
    const int y1 = (int)mv_shade_min((float)(s->rows-1),max_y);
    const float inverse_area = mv_shade_recip(area);
    const float dx0=b.y-c.y, dx1=c.y-a.y, dx2=a.y-b.y;
    const float dz=(dx0*a.z + dx1*b.z + dx2*c.z)*inverse_area;
    for (int y=y0; y<=y1; ++y) {
        if (((y-y0)&15)==0 && s->service && !s->service()) return false;
        float e0=mv_shade_edge(b,c,(float)x0+0.5f,(float)y+0.5f);
        float e1=mv_shade_edge(c,a,(float)x0+0.5f,(float)y+0.5f);
        float e2=mv_shade_edge(a,b,(float)x0+0.5f,(float)y+0.5f);
        float z=(e0*a.z + e1*b.z + e2*c.z)*inverse_area;
        size_t offset=(size_t)y*(size_t)s->cols+(size_t)x0;
        for (int x=x0; x<=x1; ++x,++offset,e0+=dx0,e1+=dx1,e2+=dx2,z+=dz) {
            if (e0 < -0.0001f || e1 < -0.0001f || e2 < -0.0001f) continue;
            const uint16_t depth=(uint16_t)mv_shade_max(1.0f,mv_shade_min(65534.0f,z));
            if (depth <= s->depth[offset]) continue;
            s->depth[offset]=depth;
            const int px=s->left+x*s->sample, py=s->top+y*s->sample;
            for (int iy=0; iy<s->sample && py+iy<s->top+s->height; ++iy)
                for (int ix=0; ix<s->sample && px+ix<s->left+s->width; ++ix)
                    pixel(buffer,px+ix,py+iy,mv_shade_black(px+ix,py+iy,density));
        }
    }
    return true;
}
#endif
