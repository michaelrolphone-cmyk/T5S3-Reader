#ifndef MODEL_VIEWER_PREVIEW_H
#define MODEL_VIEWER_PREVIEW_H

/* Orthographic pan/zoom changes only a 2-D transform, not visibility or face
 * lighting. Cache an entire orientation, not the cropped screen, and resample
 * its undithered ink. Redither at destination coordinates so patterns never
 * grow, slide or crawl with the model. The exact mesh renderer remains the
 * rotation/idle path; this bounded cache is an optional moving preview. */
#include "model_viewer_shading.h"

#define MV_PREVIEW_SIDE 640
#define MV_PREVIEW_CENTER 320.0f
#define MV_PREVIEW_SCALE 319.0f
#define MV_PREVIEW_LEVELS 3
#define MV_PREVIEW_BYTES ((size_t)(640*640 + 320*320 + 160*160))

typedef struct {
    uint8_t *ink;
    float yaw, pitch;
    bool shaded, valid, unavailable;
} mv_preview_t;

static bool mv_preview_matches(const mv_preview_t *p, float yaw, float pitch, bool shaded) {
    return p->ink && p->valid && p->yaw == yaw && p->pitch == pitch && p->shaded == shaded;
}

static mv_shade_vertex_t mv_preview_project(mv_shade_vertex_t v) {
    return (mv_shade_vertex_t){MV_PREVIEW_CENTER + v.x * MV_PREVIEW_SCALE,
                              MV_PREVIEW_CENTER - v.y * MV_PREVIEW_SCALE, v.z};
}

/* A normalized model is inside [-.5,.5]^3 before rotation; its projection fits
 * inside the atlas's [-1,1] square, with a transparent/white border. Checked
 * writes are still required for malformed coordinates and clipping roundoff.
 * Rendering is synchronous on the app owner task, like mv_shade_triangle. */
static uint8_t mv_preview_face_ink;
static void mv_preview_ink_pixel(uint8_t *buffer, int x, int y, bool black) {
    (void)black;
    if ((unsigned)x < MV_PREVIEW_SIDE && (unsigned)y < MV_PREVIEW_SIDE)
        buffer[(size_t)y * MV_PREVIEW_SIDE + x] = mv_preview_face_ink;
}

static bool mv_preview_triangle(mv_shade_surface_t *surface, uint8_t *ink,
                                mv_shade_vertex_t a, mv_shade_vertex_t b,
                                mv_shade_vertex_t c, uint8_t density) {
    mv_preview_face_ink = density >= 16u ? 255u : (uint8_t)(density * 16u);
    return mv_shade_triangle(surface, ink, mv_preview_ink_pixel, a, b, c, density);
}

/* Atlas wire edges never use the screen's portrait viewport clipper. Bound
 * coordinates before integer conversion and the loop to one atlas diagonal.
 * With normalized model vertices, both endpoints are inside this rectangle. */
static void mv_preview_line(uint8_t *ink, mv_shade_vertex_t a, mv_shade_vertex_t b) {
    if (!mv_shade_finite(a.x) || !mv_shade_finite(a.y) ||
        !mv_shade_finite(b.x) || !mv_shade_finite(b.y) ||
        a.x < 0 || a.x >= MV_PREVIEW_SIDE || a.y < 0 || a.y >= MV_PREVIEW_SIDE ||
        b.x < 0 || b.x >= MV_PREVIEW_SIDE || b.y < 0 || b.y >= MV_PREVIEW_SIDE) return;
    int x = (int)a.x, y = (int)a.y, end_x = (int)b.x, end_y = (int)b.y;
    const int sx = x < end_x ? 1 : -1, sy = y < end_y ? 1 : -1;
    const int dx = (end_x - x) * sx, dy = -(end_y - y) * sy;
    int error = dx + dy;
    for (int n = 0; n < MV_PREVIEW_SIDE; ++n) {
        ink[(size_t)y * MV_PREVIEW_SIDE + x] = 255u;
        if (x == end_x && y == end_y) break;
        const int twice = error * 2;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}

static bool mv_preview_clear(mv_preview_t *p, mv_shade_service_fn service) {
    p->valid = false;
    if (!p->ink) return false;
    for (int y = 0; y < MV_PREVIEW_SIDE; y += 8) {
        if (service && !service()) return false;
        memset(p->ink + (size_t)y * MV_PREVIEW_SIDE, 0, 8u * MV_PREVIEW_SIDE);
    }
    return true;
}

/* Two small mip levels preserve thin wire edges and surface coverage on zoom
 * out. No additional allocation, per-frame mesh pass, or frame history. */
static bool mv_preview_finish(mv_preview_t *p, float yaw, float pitch, bool shaded,
                              mv_shade_service_fn service) {
    if (!p->ink) return false;
    p->valid = false;
    const uint8_t *source = p->ink;
    int side = MV_PREVIEW_SIDE;
    for (int level = 1; level < MV_PREVIEW_LEVELS; ++level) {
        uint8_t *dest = (uint8_t *)source + (size_t)side * side;
        const int next = side / 2;
        for (int y = 0; y < next; ++y) {
            if ((y & 7) == 0 && service && !service()) return false;
            const uint8_t *row = source + (size_t)(y * 2) * side;
            for (int x = 0; x < next; ++x)
                dest[(size_t)y * next + x] = (uint8_t)((row[x*2] + row[x*2+1] +
                    row[side+x*2] + row[side+x*2+1] + 2u) >> 2);
        }
        source = dest;
        side = next;
    }
    p->yaw = yaw; p->pitch = pitch; p->shaded = shaded;
    p->valid = true;
    return true;
}

/* Fixed-point bilinear filtering retains fractional pan/zoom movement. Four
 * byte reads per destination sample; no floating-point work inside the row.
 * Coordinates are texel-center coordinates. White borders are intentional. */
static uint8_t mv_preview_sample(const uint8_t *ink, int side, int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x >= (side-1)*65536 || y >= (side-1)*65536) return 0;
    const unsigned fx = ((uint32_t)x >> 8) & 255u, fy = ((uint32_t)y >> 8) & 255u;
    const size_t index = (size_t)((uint32_t)y >> 16) * side + ((uint32_t)x >> 16);
    const uint32_t top = ink[index]*(256u-fx) + ink[index+1]*fx;
    const uint32_t bottom = ink[index+side]*(256u-fx) + ink[index+side+1]*fx;
    return (uint8_t)((top*(256u-fy) + bottom*fy + 32768u) >> 16);
}

/* Cache the two source rows in bounded stack scratch. A zoomed-in frame
 * reuses them across many destination rows rather than issuing four PSRAM
 * reads per output pixel. At most two contiguous row copies per mip change. */
typedef struct {
    int row;
    uint8_t pixels[2 * MV_PREVIEW_SIDE];
} mv_preview_rows_t;

static bool mv_preview_rows(mv_preview_rows_t *cache, const uint8_t *ink,
                            int side, int32_t y) {
    if (y < 0 || y >= (side-1)*65536) return false;
    const int row = (int)((uint32_t)y >> 16);
    if (cache->row != row) {
        memcpy(cache->pixels, ink + (size_t)row * side, (size_t)side * 2u);
        cache->row = row;
    }
    return true;
}

static uint8_t mv_preview_row_sample(const mv_preview_rows_t *cache, int side,
                                     int32_t x, int32_t y) {
    if (x < 0 || x >= (side-1)*65536) return 0;
    const unsigned index = (uint32_t)x >> 16;
    const unsigned fx = ((uint32_t)x >> 8) & 255u, fy = ((uint32_t)y >> 8) & 255u;
    const uint32_t top = cache->pixels[index]*(256u-fx) + cache->pixels[index+1]*fx;
    const uint32_t bottom = cache->pixels[index+side]*(256u-fx) + cache->pixels[index+side+1]*fx;
    return (uint8_t)((top*(256u-fy) + bottom*fy + 32768u) >> 16);
}

/* The caller clears the destination viewport before this pass. Only black
 * pixels need writes; white is already present, including uncovered areas.
 * Blend adjacent mip levels to avoid a detail pop when crossing a zoom level. */
static bool mv_preview_draw(const mv_preview_t *p, uint8_t *buffer, mv_shade_pixel_fn pixel,
                            int left, int top, int width, int height,
                            float center_x, float center_y, float scale,
                            mv_shade_service_fn service) {
    if (!p->ink || !p->valid || !buffer || !pixel ||
        width < 1 || width > 4096 || height < 1 || height > 4096 ||
        left < 0 || left > 4096 || top < 0 || top > 4096 ||
        !mv_shade_finite(center_x) || !mv_shade_finite(center_y) ||
        mv_shade_abs(center_x) > 16384 || mv_shade_abs(center_y) > 16384 ||
        !mv_shade_finite(scale) || scale < 64 || scale > 4096) return false;
    const uint8_t *ink = p->ink;
    int side = MV_PREVIEW_SIDE, level = 0;
    float factor = MV_PREVIEW_SCALE * mv_shade_recip(scale), center = MV_PREVIEW_CENTER;
    while (level + 1 < MV_PREVIEW_LEVELS && factor >= 2.0f) {
        ink += (size_t)side * side; side /= 2; ++level;
        factor *= 0.5f; center *= 0.5f;
    }
    const unsigned blend = level + 1 < MV_PREVIEW_LEVELS && factor > 1.0f ?
                           (unsigned)((factor-1.0f)*256.0f) : 0u;
    const uint8_t *lower = blend ? ink + (size_t)side * side : NULL;
    mv_preview_rows_t rows = {.row = -1}, low_rows = {.row = -1};
    /* Map destination pixel centers to atlas texel centers. The validated
     * bounds keep row starts and all increments within signed 32-bit range. */
    const int32_t dx = (int32_t)(factor * 65536.0f + 0.5f);
    const int32_t x0 = (int32_t)((center + ((float)left + 0.5f - center_x)*factor - 0.5f)*65536.0f);
    const int32_t y0 = (int32_t)((center + ((float)top + 0.5f - center_y)*factor - 0.5f)*65536.0f);
    static const uint8_t bayer[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    for (int y = 0; y < height; ++y) {
        if ((y & 7) == 0 && service && !service()) return false;
        const int32_t source_y = y0 + y*dx;
        const bool row_valid = mv_preview_rows(&rows, ink, side, source_y);
        const int32_t low_y = source_y/2 - 16384;
        const bool low_valid = blend && mv_preview_rows(&low_rows, lower, side/2, low_y);
        if (!row_valid && !low_valid) continue;
        for (int x = 0; x < width; ++x) {
            const int32_t source_x = x0 + x*dx;
            uint32_t density = row_valid ? mv_preview_row_sample(&rows, side, source_x, source_y) : 0u;
            if (blend) {
                const uint32_t low = low_valid ?
                    mv_preview_row_sample(&low_rows, side/2, source_x/2 - 16384, low_y) : 0u;
                density = (density*(256u-blend) + low*blend + 128u) >> 8;
            }
            const unsigned rank = bayer[((unsigned)(top+y)&3u)*4u + ((unsigned)(left+x)&3u)];
            if (density > rank*16u+7u) pixel(buffer, left+x, top+y, true);
        }
    }
    return true;
}
#endif
