#include "T5AppApi.h"
#include "T5FileOpenApi.h"
#include "T5HardwareTakeover.h"
#include "T5ProviderCapabilityApi.h"
#include "T5StorageApi.h"
#include "T5VideoApi.h"
#include "RiscTouchV1.h"
#include "model_viewer_shading.h"
#include "model_viewer_controls.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MV_LOGICAL_W 540
#define MV_LOGICAL_H 960
#define MV_VIEW_TOP 78
#define MV_VIEW_BOTTOM 876
#define MV_SHADE_LEFT (MV_LOGICAL_W - 174)
#define MV_SHADE_RIGHT (MV_LOGICAL_W - 12)
#define MV_SHADE_TOP 8
#define MV_SHADE_BOTTOM 56
#define MV_DEPTH_COUNT ((size_t)(MV_LOGICAL_W - 8) * (MV_VIEW_BOTTOM - MV_VIEW_TOP + 1))
#define MV_LINE_CAP 1024
#define MV_STREAM_CHUNK 768
#define MV_MAX_TRIANGLES 80000u
#define MV_INTERACTIVE_TRI_BUDGET 4500u
#define MV_REFINED_TRI_BUDGET 24000u
#define MV_TOUCH_MOVE_EPS 1
#define MV_DOUBLE_TAP_MS 380u
#define MV_TAP_MAX_MS 260u
#define MV_TAP_MOVE_PX 14
#define MV_REFINE_DELAY_MS 150u
#define MV_MAPPED_CAMERA_BUTTONS (T5_APP_BUTTON_CONFIRM | T5_APP_BUTTON_LEFT | T5_APP_BUTTON_RIGHT | T5_APP_BUTTON_UP | T5_APP_BUTTON_DOWN)
#define MV_PI 3.14159265358979323846f
#define MV_TWO_PI 6.28318530717958647692f

typedef struct { float x, y, z; } mv_vec3_t;
typedef struct { mv_vec3_t a, b, c; } mv_triangle_t;

typedef struct {
    mv_triangle_t *triangles;
    uint32_t triangle_count;
    mv_vec3_t min;
    mv_vec3_t max;
    mv_vec3_t center;
    float normalize;
} mv_model_t;

typedef struct {
    t5_storage_stream_t handle;
    size_t size;
    uint8_t chunk[MV_STREAM_CHUNK];
    size_t pos;
    size_t len;
} mv_reader_t;

typedef struct {
    uint8_t count;
    uint8_t id[2];
    int x[2];
    int y[2];
} mv_contacts_t;

typedef struct {
    float yaw;
    float pitch;
    float zoom;
    float pan_x;
    float pan_y;
} mv_view_t;

static const t5_app_api_v1 *g_app;
static const t5_storage_api_v1 *g_storage;
static const t5_file_open_api_v1 *g_file_open;
static const t5_video_api_v1 *g_video;
static const t5_provider_capability_api_v1 *g_caps;
static const risc_touch_api_v1 *g_touch;
static t5_provider_capability_lease_t g_touch_lease;
static uint64_t g_touch_subscription;
static t5_video_surface_v1 g_surface;
static bool g_use_psram;
static mv_model_t g_model;
static mv_view_t g_view;
static char g_path[T5_FILE_OPEN_PATH_MAX];
static char g_status[96];
static uint32_t g_last_interaction_ms;
static bool g_need_refine;
static bool g_touch_was_down;
static uint32_t g_touch_down_ms;
static int g_touch_down_x;
static int g_touch_down_y;
static uint32_t g_last_tap_ms;
static bool g_shaded;
static uint16_t *g_depth;
static bool g_shade_capture;
static bool g_shade_tap;
static uint8_t g_shade_touch_id;
static t5_app_input_t g_render_input;
static bool g_render_input_pending;
static uint32_t g_render_service_ms;
static mv_controller_t g_controller;
static mv_motion_t g_motion;
static bool g_draw_pending;
static bool g_render_interactive;

static int mv_iabs(int v) { return v < 0 ? -v : v; }
static float mv_fabs(float v) { return v < 0.0f ? -v : v; }
static float mv_maxf(float a, float b) { return a > b ? a : b; }
static float mv_minf(float a, float b) { return a < b ? a : b; }
static float mv_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static float mv_wrap_angle(float a) {
    while (a > MV_PI) a -= MV_TWO_PI;
    while (a < -MV_PI) a += MV_TWO_PI;
    return a;
}

/* Fast enough for interactive wireframe rotation and independent of libm,
 * which is intentionally not part of the ordinary native-app import ABI. */
static float mv_sin(float x) {
    x = mv_wrap_angle(x);
    const float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * (0.0083333310f - x2 * 0.00019840874f)));
}
static float mv_cos(float x) { return mv_sin(x + 1.57079632679489661923f); }

static bool mv_float_finite(float v) {
    return v == v && v > -1.0e20f && v < 1.0e20f;
}

static float mv_reciprocal_positive(float value) {
    uint32_t bits = 0;
    float estimate = 0.0f;
    memcpy(&bits, &value, sizeof(bits));
    bits = 0x7ef311c3u - bits;
    memcpy(&estimate, &bits, sizeof(estimate));
    estimate = estimate * (2.0f - value * estimate);
    estimate = estimate * (2.0f - value * estimate);
    estimate = estimate * (2.0f - value * estimate);
    return estimate;
}

static void *mv_alloc(size_t bytes) {
    if (!bytes) return NULL;
    if (g_use_psram && g_app && g_app->psram_alloc) return g_app->psram_alloc(bytes);
    return malloc(bytes);
}

static void mv_free(void *ptr) {
    if (!ptr) return;
    if (g_use_psram && g_app && g_app->psram_free) g_app->psram_free(ptr);
    else free(ptr);
}

static void mv_model_free(mv_model_t *model) {
    if (!model) return;
    mv_free(model->triangles);
    memset(model, 0, sizeof(*model));
}

static void mv_bounds_reset(mv_model_t *model) {
    model->min.x = model->min.y = model->min.z = 1.0e30f;
    model->max.x = model->max.y = model->max.z = -1.0e30f;
}

static void mv_bounds_add(mv_model_t *model, mv_vec3_t v) {
    model->min.x = mv_minf(model->min.x, v.x);
    model->min.y = mv_minf(model->min.y, v.y);
    model->min.z = mv_minf(model->min.z, v.z);
    model->max.x = mv_maxf(model->max.x, v.x);
    model->max.y = mv_maxf(model->max.y, v.y);
    model->max.z = mv_maxf(model->max.z, v.z);
}

static bool mv_model_finalize(mv_model_t *model) {
    if (!model || !model->triangles || !model->triangle_count) return false;
    model->center.x = (model->min.x + model->max.x) * 0.5f;
    model->center.y = (model->min.y + model->max.y) * 0.5f;
    model->center.z = (model->min.z + model->max.z) * 0.5f;
    float extent = mv_maxf(model->max.x - model->min.x, model->max.y - model->min.y);
    extent = mv_maxf(extent, model->max.z - model->min.z);
    if (!mv_float_finite(extent) || extent < 0.000001f) return false;
    model->normalize = mv_reciprocal_positive(extent);
    return true;
}

static bool mv_reader_open(mv_reader_t *reader, const char *path) {
    memset(reader, 0, sizeof(*reader));
    reader->handle = g_storage->stream_open(path, &reader->size);
    return reader->handle != T5_STORAGE_STREAM_INVALID;
}

static void mv_reader_close(mv_reader_t *reader) {
    if (reader->handle != T5_STORAGE_STREAM_INVALID) g_storage->stream_close(reader->handle);
    memset(reader, 0, sizeof(*reader));
}

static bool mv_reader_seek(mv_reader_t *reader, size_t offset) {
    if (!g_storage->stream_seek(reader->handle, offset)) return false;
    reader->pos = reader->len = 0;
    return true;
}

static int mv_reader_getc(mv_reader_t *reader) {
    if (reader->pos >= reader->len) {
        reader->len = g_storage->stream_read(reader->handle, reader->chunk, sizeof(reader->chunk));
        reader->pos = 0;
        if (!reader->len) return -1;
    }
    return reader->chunk[reader->pos++];
}

static bool mv_reader_line(mv_reader_t *reader, char *line, size_t capacity) {
    if (!capacity) return false;
    size_t n = 0;
    bool got = false;
    for (;;) {
        const int ch = mv_reader_getc(reader);
        if (ch < 0) break;
        got = true;
        if (ch == '\n') break;
        if (ch == '\r') continue;
        if (n + 1 < capacity) line[n++] = (char)ch;
    }
    line[n] = 0;
    return got;
}

static bool mv_reader_exact(mv_reader_t *reader, void *buffer, size_t size) {
    uint8_t *out = (uint8_t *)buffer;
    size_t done = 0;
    while (done < size) {
        const size_t got = g_storage->stream_read(reader->handle, out + done, size - done);
        if (!got) return false;
        done += got;
    }
    reader->pos = reader->len = 0;
    return true;
}

static const char *mv_skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static bool mv_parse_float(const char **cursor, float *value) {
    const char *p = mv_skip_ws(*cursor);
    bool negative = false;
    if (*p == '+' || *p == '-') {
        negative = *p == '-';
        ++p;
    }
    bool any = false;
    float result = 0.0f;
    while (*p >= '0' && *p <= '9') {
        result = result * 10.0f + (float)(*p - '0');
        ++p;
        any = true;
    }
    if (*p == '.') {
        ++p;
        float place = 0.1f;
        while (*p >= '0' && *p <= '9') {
            result += (float)(*p - '0') * place;
            place *= 0.1f;
            ++p;
            any = true;
        }
    }
    if (!any) return false;
    if (*p == 'e' || *p == 'E') {
        ++p;
        bool exp_negative = false;
        if (*p == '+' || *p == '-') {
            exp_negative = *p == '-';
            ++p;
        }
        int exp = 0;
        bool exp_any = false;
        while (*p >= '0' && *p <= '9') {
            exp = exp * 10 + (*p - '0');
            if (exp > 38) exp = 38;
            ++p;
            exp_any = true;
        }
        if (!exp_any) return false;
        while (exp-- > 0) result = exp_negative ? result * 0.1f : result * 10.0f;
    }
    if (negative) result = -result;
    if (!mv_float_finite(result)) return false;
    *value = result;
    *cursor = p;
    return true;
}

static bool mv_parse_int(const char **cursor, int32_t *value) {
    const char *p = mv_skip_ws(*cursor);
    bool negative = false;
    if (*p == '+' || *p == '-') {
        negative = *p == '-';
        ++p;
    }
    if (*p < '0' || *p > '9') return false;
    int32_t result = 0;
    while (*p >= '0' && *p <= '9') {
        if (result > 200000000) return false;
        result = result * 10 + (*p - '0');
        ++p;
    }
    *value = negative ? -result : result;
    *cursor = p;
    return true;
}

static bool mv_line_vertex(const char *line, mv_vec3_t *out) {
    const char *p = mv_skip_ws(line);
    if (*p != 'v' || (p[1] != ' ' && p[1] != '\t')) return false;
    ++p;
    return mv_parse_float(&p, &out->x) && mv_parse_float(&p, &out->y) &&
           mv_parse_float(&p, &out->z);
}

static uint32_t mv_obj_face_tokens(const char *line) {
    const char *p = mv_skip_ws(line);
    if (*p != 'f' || (p[1] != ' ' && p[1] != '\t')) return 0;
    ++p;
    uint32_t count = 0;
    for (;;) {
        p = mv_skip_ws(p);
        if (!*p) break;
        int32_t index = 0;
        if (!mv_parse_int(&p, &index)) return 0;
        (void)index;
        while (*p && *p != ' ' && *p != '\t') ++p;
        ++count;
    }
    return count;
}

static int32_t mv_obj_index(const char **cursor, uint32_t vertex_count) {
    int32_t raw = 0;
    if (!mv_parse_int(cursor, &raw) || raw == 0) return -1;
    int32_t resolved = raw > 0 ? raw - 1 : (int32_t)vertex_count + raw;
    while (**cursor && **cursor != ' ' && **cursor != '\t') ++(*cursor);
    if (resolved < 0 || (uint32_t)resolved >= vertex_count) return -1;
    return resolved;
}

static void mv_service_parse(void) {
    if (!g_app || !g_app->poll) return;
    t5_app_input_t ignored = {0};
    (void)g_app->poll(&ignored, 1);
}

static bool mv_load_obj(mv_reader_t *reader, mv_model_t *model) {
    char line[MV_LINE_CAP];
    uint32_t vertex_count = 0;
    uint32_t triangle_count = 0;
    uint32_t lines = 0;
    if (!mv_reader_seek(reader, 0)) return false;
    while (mv_reader_line(reader, line, sizeof(line))) {
        mv_vec3_t v;
        if (mv_line_vertex(line, &v)) {
            if (++vertex_count > MV_MAX_TRIANGLES * 3u) return false;
        } else {
            const uint32_t tokens = mv_obj_face_tokens(line);
            if (tokens >= 3u) {
                const uint32_t add = tokens - 2u;
                if (triangle_count > MV_MAX_TRIANGLES - add) return false;
                triangle_count += add;
            }
        }
        if ((++lines & 511u) == 0u) mv_service_parse();
    }
    if (!vertex_count || !triangle_count) return false;

    mv_vec3_t *vertices = (mv_vec3_t *)mv_alloc((size_t)vertex_count * sizeof(mv_vec3_t));
    model->triangles = (mv_triangle_t *)mv_alloc((size_t)triangle_count * sizeof(mv_triangle_t));
    if (!vertices || !model->triangles) {
        mv_free(vertices);
        mv_model_free(model);
        return false;
    }

    uint32_t vertices_used = 0;
    uint32_t triangles_used = 0;
    mv_bounds_reset(model);
    if (!mv_reader_seek(reader, 0)) {
        mv_free(vertices);
        mv_model_free(model);
        return false;
    }

    lines = 0;
    while (mv_reader_line(reader, line, sizeof(line))) {
        mv_vec3_t v;
        if (mv_line_vertex(line, &v)) {
            if (vertices_used >= vertex_count) break;
            vertices[vertices_used++] = v;
            mv_bounds_add(model, v);
        } else {
            const char *p = mv_skip_ws(line);
            if (*p == 'f' && (p[1] == ' ' || p[1] == '\t')) {
                ++p;
                p = mv_skip_ws(p);
                int32_t first = mv_obj_index(&p, vertices_used);
                p = mv_skip_ws(p);
                int32_t previous = mv_obj_index(&p, vertices_used);
                if (first >= 0 && previous >= 0) {
                    for (;;) {
                        p = mv_skip_ws(p);
                        if (!*p) break;
                        const int32_t current = mv_obj_index(&p, vertices_used);
                        if (current < 0) break;
                        if (triangles_used >= triangle_count) break;
                        mv_triangle_t *t = &model->triangles[triangles_used++];
                        t->a = vertices[first];
                        t->b = vertices[previous];
                        t->c = vertices[current];
                        previous = current;
                    }
                }
            }
        }
        if ((++lines & 511u) == 0u) mv_service_parse();
    }

    mv_free(vertices);
    model->triangle_count = triangles_used;
    return triangles_used != 0u && mv_model_finalize(model);
}

static uint32_t mv_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float mv_f32_le(const uint8_t *p) {
    uint32_t bits = mv_u32_le(p);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool mv_stl_binary_layout(mv_reader_t *reader, uint32_t *triangles) {
    uint8_t header[84];
    if (!mv_reader_seek(reader, 0) || !mv_reader_exact(reader, header, sizeof(header))) return false;
    const uint32_t count = mv_u32_le(header + 80);
    if (!count || count > MV_MAX_TRIANGLES) return false;
    if (count > (SIZE_MAX - 84u) / 50u) return false;
    if (84u + (size_t)count * 50u != reader->size) return false;
    *triangles = count;
    return true;
}

static bool mv_load_binary_stl(mv_reader_t *reader, mv_model_t *model, uint32_t count) {
    uint8_t header[84];
    uint8_t record[50];
    if (!mv_reader_seek(reader, 0) || !mv_reader_exact(reader, header, sizeof(header))) return false;
    model->triangles = (mv_triangle_t *)mv_alloc((size_t)count * sizeof(mv_triangle_t));
    if (!model->triangles) return false;
    mv_bounds_reset(model);

    for (uint32_t i = 0; i < count; ++i) {
        if (!mv_reader_exact(reader, record, sizeof(record))) {
            mv_model_free(model);
            return false;
        }
        mv_triangle_t *t = &model->triangles[i];
        t->a.x = mv_f32_le(record + 12); t->a.y = mv_f32_le(record + 16); t->a.z = mv_f32_le(record + 20);
        t->b.x = mv_f32_le(record + 24); t->b.y = mv_f32_le(record + 28); t->b.z = mv_f32_le(record + 32);
        t->c.x = mv_f32_le(record + 36); t->c.y = mv_f32_le(record + 40); t->c.z = mv_f32_le(record + 44);
        if (!mv_float_finite(t->a.x) || !mv_float_finite(t->a.y) || !mv_float_finite(t->a.z) ||
            !mv_float_finite(t->b.x) || !mv_float_finite(t->b.y) || !mv_float_finite(t->b.z) ||
            !mv_float_finite(t->c.x) || !mv_float_finite(t->c.y) || !mv_float_finite(t->c.z)) {
            mv_model_free(model);
            return false;
        }
        mv_bounds_add(model, t->a);
        mv_bounds_add(model, t->b);
        mv_bounds_add(model, t->c);
        if ((i & 511u) == 0u) mv_service_parse();
    }
    model->triangle_count = count;
    return mv_model_finalize(model);
}

static bool mv_ascii_stl_vertex(const char *line, mv_vec3_t *out) {
    const char *p = mv_skip_ws(line);
    static const char key[] = "vertex";
    if (strncmp(p, key, sizeof(key) - 1u) != 0) return false;
    p += sizeof(key) - 1u;
    if (*p != ' ' && *p != '\t') return false;
    return mv_parse_float(&p, &out->x) && mv_parse_float(&p, &out->y) &&
           mv_parse_float(&p, &out->z);
}

static bool mv_load_ascii_stl(mv_reader_t *reader, mv_model_t *model) {
    char line[MV_LINE_CAP];
    uint32_t vertex_lines = 0;
    uint32_t lines = 0;
    if (!mv_reader_seek(reader, 0)) return false;
    while (mv_reader_line(reader, line, sizeof(line))) {
        mv_vec3_t v;
        if (mv_ascii_stl_vertex(line, &v)) {
            if (++vertex_lines > MV_MAX_TRIANGLES * 3u) return false;
        }
        if ((++lines & 511u) == 0u) mv_service_parse();
    }
    const uint32_t count = vertex_lines / 3u;
    if (!count || count > MV_MAX_TRIANGLES) return false;

    model->triangles = (mv_triangle_t *)mv_alloc((size_t)count * sizeof(mv_triangle_t));
    if (!model->triangles) return false;
    mv_bounds_reset(model);
    if (!mv_reader_seek(reader, 0)) {
        mv_model_free(model);
        return false;
    }

    uint32_t used = 0;
    uint8_t corner = 0;
    mv_triangle_t current;
    lines = 0;
    while (mv_reader_line(reader, line, sizeof(line))) {
        mv_vec3_t v;
        if (mv_ascii_stl_vertex(line, &v)) {
            if (corner == 0) current.a = v;
            else if (corner == 1) current.b = v;
            else {
                current.c = v;
                if (used >= count) break;
                model->triangles[used++] = current;
                mv_bounds_add(model, current.a);
                mv_bounds_add(model, current.b);
                mv_bounds_add(model, current.c);
            }
            corner = (uint8_t)((corner + 1u) % 3u);
        }
        if ((++lines & 511u) == 0u) mv_service_parse();
    }
    model->triangle_count = used;
    return used != 0u && mv_model_finalize(model);
}

static char mv_lower(char ch) { return ch >= 'A' && ch <= 'Z' ? (char)(ch + 32) : ch; }

static bool mv_ends_with_ci(const char *value, const char *suffix) {
    const size_t n = strlen(value);
    const size_t s = strlen(suffix);
    if (n < s) return false;
    for (size_t i = 0; i < s; ++i)
        if (mv_lower(value[n - s + i]) != mv_lower(suffix[i])) return false;
    return true;
}

static bool mv_load_model(const char *path, mv_model_t *model) {
    mv_reader_t reader;
    if (!mv_reader_open(&reader, path)) return false;
    bool ok = false;
    if (mv_ends_with_ci(path, ".obj")) {
        ok = mv_load_obj(&reader, model);
    } else if (mv_ends_with_ci(path, ".stl")) {
        uint32_t binary_count = 0;
        ok = mv_stl_binary_layout(&reader, &binary_count)
                 ? mv_load_binary_stl(&reader, model, binary_count)
                 : mv_load_ascii_stl(&reader, model);
    }
    mv_reader_close(&reader);
    return ok;
}

/* The fast-video surface is physical landscape 960x540. RiscRTE's touch/UI
 * coordinate space is portrait 540x960. Match GameBoy's proven mapping:
 * logical (x,y) -> panel (y, 539-x). */
static void mv_pixel(uint8_t *buffer, int x, int y, bool black) {
    if (!buffer || x < 0 || y < 0 || x >= MV_LOGICAL_W || y >= MV_LOGICAL_H) return;
    const int panel_x = y;
    const int panel_y = (MV_LOGICAL_W - 1) - x;
    const size_t offset = (size_t)panel_y * g_surface.stride_bytes + (size_t)(panel_x >> 3);
    const uint8_t mask = (uint8_t)(0x80u >> (panel_x & 7));
    if (black) buffer[offset] |= mask;
    else buffer[offset] &= (uint8_t)~mask;
}

static uint8_t const *mv_glyph(char ch) {
    static const uint8_t blank[5] = {0,0,0,0,0};
    static const uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
        {0x62,0x51,0x49,0x49,0x46},{0x22,0x49,0x49,0x49,0x36},
        {0x18,0x14,0x12,0x7F,0x10},{0x2F,0x49,0x49,0x49,0x31},
        {0x3E,0x49,0x49,0x49,0x32},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x26,0x49,0x49,0x49,0x3E}
    };
    static const uint8_t letters[26][5] = {
        {0x7E,0x09,0x09,0x09,0x7E},{0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
        {0x26,0x49,0x49,0x49,0x32},{0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
        {0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
        {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t dot[5] = {0,0x60,0x60,0,0};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t colon[5] = {0,0x36,0x36,0,0};
    static const uint8_t plus[5] = {0x08,0x08,0x3E,0x08,0x08};
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch == '-') return dash;
    if (ch == '.') return dot;
    if (ch == '/') return slash;
    if (ch == ':') return colon;
    if (ch == '+') return plus;
    return blank;
}

static void mv_text_ink(uint8_t *buffer, int x, int y, const char *text, int scale, bool black) {
    for (const char *p = text; p && *p; ++p) {
        const uint8_t *glyph = mv_glyph(*p);
        for (int cx = 0; cx < 5; ++cx) {
            for (int cy = 0; cy < 7; ++cy) {
                if ((glyph[cx] & (1u << cy)) == 0u) continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx)
                        mv_pixel(buffer, x + cx * scale + sx, y + cy * scale + sy, black);
            }
        }
        x += 6 * scale;
    }
}

static void mv_text(uint8_t *buffer, int x, int y, const char *text, int scale) {
    mv_text_ink(buffer,x,y,text,scale,true);
}

static bool mv_shade_button_hit(int x, int y) {
    return x>=MV_SHADE_LEFT && x<MV_SHADE_RIGHT && y>=MV_SHADE_TOP && y<MV_SHADE_BOTTOM;
}

static void mv_shade_button_draw(uint8_t *buffer) {
    const int w=MV_SHADE_RIGHT-MV_SHADE_LEFT, h=MV_SHADE_BOTTOM-MV_SHADE_TOP;
    for (int y=0; y<h; ++y) {
        for (int x=0; x<w; ++x) {
            const int dx=x<7?7-x:(x>=w-7?x-(w-8):0);
            const int dy=y<7?7-y:(y>=h-7?y-(h-8):0);
            if (dx*dx+dy*dy>49) continue;
            const bool border=x<2 || x>=w-2 || y<2 || y>=h-2 || dx*dx+dy*dy>25;
            mv_pixel(buffer,MV_SHADE_LEFT+x,MV_SHADE_TOP+y,g_shaded || border);
        }
    }
    const char *label=g_shaded?"SHADE ON":"SHADE OFF";
    const int text_w=(int)strlen(label)*12-2;
    mv_text_ink(buffer,MV_SHADE_LEFT+(w-text_w)/2,MV_SHADE_TOP+(h-14)/2,label,2,!g_shaded);
}

static void mv_shade_release(void) {
    mv_free(g_depth);
    g_depth=NULL;
    g_shaded=false;
}

static void mv_shade_toggle(void) {
    g_status[0]=0;
    if (g_shaded) { mv_shade_release(); return; }
    /* Never consume internal SRAM for the large optional depth buffer. */
    if (g_use_psram) g_depth=(uint16_t *)mv_alloc(MV_DEPTH_COUNT*sizeof(*g_depth));
    if (!g_depth) {
        snprintf(g_status,sizeof(g_status),"SHADING UNAVAILABLE - NOT ENOUGH PSRAM");
        return;
    }
    g_shaded=true;
}

enum { MV_CLIP_LEFT=1, MV_CLIP_RIGHT=2, MV_CLIP_TOP=4, MV_CLIP_BOTTOM=8 };

static int mv_clip_code(int x, int y) {
    int code = 0;
    if (x < 4) code |= MV_CLIP_LEFT;
    else if (x >= MV_LOGICAL_W - 4) code |= MV_CLIP_RIGHT;
    if (y < MV_VIEW_TOP) code |= MV_CLIP_TOP;
    else if (y > MV_VIEW_BOTTOM) code |= MV_CLIP_BOTTOM;
    return code;
}

static bool mv_clip_line(int *x0, int *y0, int *x1, int *y1) {
    int c0 = mv_clip_code(*x0, *y0);
    int c1 = mv_clip_code(*x1, *y1);
    for (int guard = 0; guard < 16; ++guard) {
        if (!(c0 | c1)) return true;
        if (c0 & c1) return false;
        const int out = c0 ? c0 : c1;
        int x = 0, y = 0;
        if (out & MV_CLIP_TOP) {
            y = MV_VIEW_TOP;
            if (*y1 == *y0) return false;
            x = *x0 + (*x1 - *x0) * (y - *y0) / (*y1 - *y0);
        } else if (out & MV_CLIP_BOTTOM) {
            y = MV_VIEW_BOTTOM;
            if (*y1 == *y0) return false;
            x = *x0 + (*x1 - *x0) * (y - *y0) / (*y1 - *y0);
        } else if (out & MV_CLIP_RIGHT) {
            x = MV_LOGICAL_W - 5;
            if (*x1 == *x0) return false;
            y = *y0 + (*y1 - *y0) * (x - *x0) / (*x1 - *x0);
        } else {
            x = 4;
            if (*x1 == *x0) return false;
            y = *y0 + (*y1 - *y0) * (x - *x0) / (*x1 - *x0);
        }
        if (out == c0) { *x0=x; *y0=y; c0=mv_clip_code(*x0,*y0); }
        else { *x1=x; *y1=y; c1=mv_clip_code(*x1,*y1); }
    }
    return false;
}

static void mv_line(uint8_t *buffer, int x0, int y0, int x1, int y1) {
    if (!mv_clip_line(&x0,&y0,&x1,&y1)) return;
    int dx = mv_iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -mv_iabs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        mv_pixel(buffer, x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

typedef struct { float cy, sy, cp, sp; } mv_rotation_t;

static mv_shade_vertex_t mv_camera_point(mv_vec3_t v, const mv_rotation_t *r) {
    const float x=(v.x-g_model.center.x)*g_model.normalize;
    const float y=(v.y-g_model.center.y)*g_model.normalize;
    const float z=(v.z-g_model.center.z)*g_model.normalize;
    const float x1=r->cy*x+r->sy*z, z1=-r->sy*x+r->cy*z;
    return (mv_shade_vertex_t){x1,r->cp*y-r->sp*z1,r->sp*y+r->cp*z1};
}

static mv_shade_vertex_t mv_screen_point(mv_shade_vertex_t v) {
    const float scale=390.0f*g_view.zoom;
    return (mv_shade_vertex_t){270.0f+g_view.pan_x+v.x*scale,
                              468.0f+g_view.pan_y-v.y*scale,v.z};
}

/* Bound long filled passes without losing controller/exit input consumed by
 * poll(). Keep a single input snapshot, not an accumulating event queue. */
static bool mv_render_service(void) {
    if ((uint32_t)(g_app->millis()-g_render_service_ms)<16u) return true;
    g_render_service_ms=g_app->millis();
    t5_app_input_t input={0};
    if (!g_app->poll(&input,1)) input.exit_requested=true;
    const uint32_t held=mv_controller_poll(&g_controller);
    mv_motion_delta_t ignored;
    (void)mv_motion_step(&g_motion,held,g_app->millis(),false,&ignored);
    if (held&MV_PAD_BACK) input.exit_requested=true;
    input.buttons=mv_controller_filter_mapped(&g_controller,input.buttons,MV_MAPPED_CAMERA_BUTTONS);
    if (input.buttons || input.exit_requested) {
        g_render_input=input;
        g_render_input_pending=true;
        return false;
    }
    /* Interrupt an idle refinement for new motion, never repeatedly abort an
     * interactive frame merely because a direction is still held. */
    if (!g_render_interactive && mv_motion_action(held)) {
        g_draw_pending=true;
        return false;
    }
    return true;
}

static const char *mv_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void mv_reset_view(void) {
    g_view.yaw = -0.55f;
    g_view.pitch = 0.38f;
    g_view.zoom = 1.0f;
    g_view.pan_x = 0.0f;
    g_view.pan_y = 0.0f;
    g_need_refine = true;
}

static uint32_t mv_render_step(bool interactive) {
    const uint32_t budget = interactive ? MV_INTERACTIVE_TRI_BUDGET : MV_REFINED_TRI_BUDGET;
    return g_model.triangle_count <= budget ? 1u :
           (g_model.triangle_count + budget - 1u) / budget;
}

static bool mv_render(bool interactive) {
    if (!g_video->can_submit()) return false;
    size_t bytes = 0;
    uint8_t *buffer = g_video->backbuffer(&bytes);
    if (!buffer || bytes < (size_t)g_surface.stride_bytes * g_surface.height) return false;
    memset(buffer, 0x00, bytes);
    g_render_interactive=interactive;

    char info[96];
    mv_text(buffer, 14, 12, "3D MODEL VIEWER", 2);
    char filename[(MV_LOGICAL_W-28)/6+1];
    snprintf(filename,sizeof(filename),"%s",mv_basename(g_path));
    mv_text(buffer,14,64,filename,1);
    mv_shade_button_draw(buffer);

    const mv_rotation_t rotation={mv_cos(g_view.yaw),mv_sin(g_view.yaw),
                                  mv_cos(g_view.pitch),mv_sin(g_view.pitch)};
    mv_shade_surface_t shade={0};
    g_render_service_ms=g_app->millis();
    if (g_shaded && !mv_shade_begin(&shade,g_depth,MV_DEPTH_COUNT,4,MV_VIEW_TOP,
                                    MV_LOGICAL_W-8,MV_VIEW_BOTTOM-MV_VIEW_TOP+1,
                                    interactive?2:1,mv_render_service)) return false;
    /* Never use triangle-stride LOD on filled geometry: it opens mesh holes. */
    const uint32_t step = g_shaded ? 1u : mv_render_step(interactive);
    for (uint32_t i = 0; i < g_model.triangle_count; i += step) {
        if ((i&63u)==0u && !mv_render_service()) return false;
        const mv_triangle_t *t = &g_model.triangles[i];
        const mv_shade_vertex_t a=mv_camera_point(t->a,&rotation);
        const mv_shade_vertex_t b=mv_camera_point(t->b,&rotation);
        const mv_shade_vertex_t c=mv_camera_point(t->c,&rotation);
        const mv_shade_vertex_t pa=mv_screen_point(a), pb=mv_screen_point(b), pc=mv_screen_point(c);
        if (g_shaded) {
            if (!mv_shade_triangle(&shade,buffer,mv_pixel,pa,pb,pc,mv_shade_density(a,b,c))) return false;
        } else {
            mv_line(buffer,(int)pa.x,(int)pa.y,(int)pb.x,(int)pb.y);
            mv_line(buffer,(int)pb.x,(int)pb.y,(int)pc.x,(int)pc.y);
            mv_line(buffer,(int)pc.x,(int)pc.y,(int)pa.x,(int)pa.y);
        }
    }

    snprintf(info,sizeof(info),"%lu TRI  %s",
             (unsigned long)g_model.triangle_count,
             interactive ? "FAST" : (step == 1u ? "FULL" : "REFINED"));
    mv_text(buffer,14,894,g_status[0]?g_status:info,1);
    mv_text(buffer,14,910,"D-PAD ROTATE  LB+UP/DOWN ZOOM  RB+D-PAD PAN",1);
    mv_text(buffer,14,926,"HOLD A: 1/4 SPEED  DRAG ROTATE  2F PAN+ZOOM",1);
    mv_text(buffer,14,942,"DOUBLE TAP RESET  BACK EXIT",1);

    return g_video->submit(0, g_surface.height);
}

static void mv_message(const char *title, const char *detail) {
    size_t bytes = 0;
    uint8_t *buffer = g_video->backbuffer(&bytes);
    if (!buffer) return;
    memset(buffer,0x00,bytes);
    mv_text(buffer,28,220,title,2);
    mv_text(buffer,28,270,detail,1);
    mv_text(buffer,28,920,"BACK EXIT",1);
    while (!g_video->submit(0,g_surface.height)) {
        t5_app_input_t ignored={0};
        if (!g_app->poll(&ignored,5)) break;
    }
}

static bool mv_touch_begin(void) {
    g_caps = t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    if (!g_caps || g_caps->struct_size < sizeof(*g_caps) || !g_caps->acquire || !g_caps->release) return false;
    const void *interface_ptr = NULL;
    g_touch_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
    if (!g_caps->acquire("input.touch.raw",RISC_TOUCH_API_V1,&g_touch_lease,&interface_ptr) ||
        !interface_ptr || g_touch_lease == T5_PROVIDER_CAPABILITY_LEASE_INVALID) return false;
    g_touch = (const risc_touch_api_v1 *)interface_ptr;
    if (g_touch->api_version != RISC_TOUCH_API_V1 || g_touch->struct_size < sizeof(*g_touch) ||
        !g_touch->subscribe || !g_touch->unsubscribe || !g_touch->poll ||
        !g_touch->next || !g_touch->snapshot) return false;
    g_touch_subscription = g_touch->subscribe(g_touch->context);
    return g_touch_subscription != 0u;
}

static void mv_touch_end(void) {
    if (g_touch && g_touch_subscription) (void)g_touch->unsubscribe(g_touch->context,g_touch_subscription);
    g_touch_subscription = 0;
    g_touch = NULL;
    if (g_caps && g_touch_lease != T5_PROVIDER_CAPABILITY_LEASE_INVALID)
        (void)g_caps->release(g_touch_lease);
    g_touch_lease = T5_PROVIDER_CAPABILITY_LEASE_INVALID;
    g_caps = NULL;
}

static bool mv_touch_snapshot(mv_contacts_t *contacts, bool *home_pressed) {
    memset(contacts,0,sizeof(*contacts));
    *home_pressed = false;
    if (!g_touch) return false;
    const bool healthy = g_touch->poll(g_touch->context,16u);
    for (unsigned i=0;i<RISC_TOUCH_QUEUE_LENGTH;++i) {
        risc_touch_event_v1 ev={0};
        const int32_t rc=g_touch->next(g_touch->context,g_touch_subscription,&ev);
        if (rc==0) break;
        if (rc<0) break;
    }
    risc_touch_snapshot_v1 snapshot={0};
    if (!g_touch->snapshot(g_touch->context,&snapshot)) return false;
    *home_pressed=(snapshot.buttons&RISC_TOUCH_BUTTON_PRIMARY)!=0u;
    contacts->count=snapshot.contact_count>2u?2u:snapshot.contact_count;
    for(uint8_t i=0;i<contacts->count;++i) {
        contacts->id[i]=snapshot.contacts[i].id;
        const uint32_t width=snapshot.width?snapshot.width:MV_LOGICAL_W;
        const uint32_t height=snapshot.height?snapshot.height:MV_LOGICAL_H;
        contacts->x[i]=(int)((uint32_t)snapshot.contacts[i].x*MV_LOGICAL_W/width);
        contacts->y[i]=(int)((uint32_t)snapshot.contacts[i].y*MV_LOGICAL_H/height);
    }
    return healthy;
}

static int mv_contact_index(const mv_contacts_t *contacts,uint8_t id) {
    for(uint8_t i=0;i<contacts->count;++i) if(contacts->id[i]==id) return i;
    return -1;
}

static int mv_approx_distance(int dx,int dy) {
    dx=mv_iabs(dx); dy=mv_iabs(dy);
    const int hi=dx>dy?dx:dy;
    const int lo=dx>dy?dy:dx;
    return hi+(lo>>1);
}

static bool mv_handle_touch(const mv_contacts_t *prev,const mv_contacts_t *now,uint32_t ms) {
    bool changed=false;
    /* Capture a header gesture from press through final release. A drag out,
     * replacement contact, or second finger cancels the tap, not the capture. */
    if (!prev->count && now->count && mv_shade_button_hit(now->x[0],now->y[0])) {
        g_shade_capture=true;
        g_shade_tap=now->count==1u;
        g_shade_touch_id=now->id[0];
        g_touch_down_ms=ms;
        g_touch_down_x=now->x[0]; g_touch_down_y=now->y[0];
        g_touch_was_down=false;
        g_last_tap_ms=0;
    }
    if (g_shade_capture) {
        if (now->count) {
            if (now->count!=1u || now->id[0]!=g_shade_touch_id ||
                !mv_shade_button_hit(now->x[0],now->y[0]) ||
                mv_iabs(now->x[0]-g_touch_down_x)>MV_TAP_MOVE_PX ||
                mv_iabs(now->y[0]-g_touch_down_y)>MV_TAP_MOVE_PX) g_shade_tap=false;
            return false;
        }
        g_shade_capture=false;
        if (g_shade_tap && (uint32_t)(ms-g_touch_down_ms)<=MV_TAP_MAX_MS) {
            mv_shade_toggle();
            g_last_interaction_ms=ms;
            g_need_refine=true;
            return true;
        }
        return false;
    }
    if(now->count==1u) {
        const int pi=mv_contact_index(prev,now->id[0]);
        if(pi>=0 && prev->count==1u) {
            const int dx=now->x[0]-prev->x[pi];
            const int dy=now->y[0]-prev->y[pi];
            if(mv_iabs(dx)>=MV_TOUCH_MOVE_EPS || mv_iabs(dy)>=MV_TOUCH_MOVE_EPS) {
                g_view.yaw=mv_wrap_angle(g_view.yaw+(float)dx*0.0105f);
                g_view.pitch=mv_wrap_angle(g_view.pitch+(float)dy*0.0105f);
                changed=true;
            }
        }
    } else if(now->count>=2u && prev->count>=2u) {
        const int p0=mv_contact_index(prev,now->id[0]);
        const int p1=mv_contact_index(prev,now->id[1]);
        if(p0>=0 && p1>=0) {
            const int cx=(now->x[0]+now->x[1])/2;
            const int cy=(now->y[0]+now->y[1])/2;
            const int pcx=(prev->x[p0]+prev->x[p1])/2;
            const int pcy=(prev->y[p0]+prev->y[p1])/2;
            const int pan_dx=cx-pcx, pan_dy=cy-pcy;
            const int nd=mv_approx_distance(now->x[0]-now->x[1],now->y[0]-now->y[1]);
            const int pd=mv_approx_distance(prev->x[p0]-prev->x[p1],prev->y[p0]-prev->y[p1]);
            const int dd=nd-pd;
            if(mv_iabs(pan_dx)>0 || mv_iabs(pan_dy)>0 || mv_iabs(dd)>0) {
                g_view.pan_x+=pan_dx;
                g_view.pan_y+=pan_dy;
                g_view.zoom=mv_clampf(g_view.zoom*(1.0f+(float)dd*0.00454545455f),0.18f,7.0f);
                changed=true;
            }
        }
    }

    if(!g_touch_was_down && now->count==1u) {
        g_touch_was_down=true;
        g_touch_down_ms=ms;
        g_touch_down_x=now->x[0];
        g_touch_down_y=now->y[0];
    } else if(g_touch_was_down && now->count==0u) {
        g_touch_was_down=false;
        if(ms-g_touch_down_ms<=MV_TAP_MAX_MS && prev->count==1u &&
           mv_iabs(prev->x[0]-g_touch_down_x)<=MV_TAP_MOVE_PX &&
           mv_iabs(prev->y[0]-g_touch_down_y)<=MV_TAP_MOVE_PX) {
            if(g_last_tap_ms && ms-g_last_tap_ms<=MV_DOUBLE_TAP_MS) {
                mv_reset_view();
                g_last_tap_ms=0;
                changed=true;
            } else {
                g_last_tap_ms=ms;
            }
        }
    }

    if(changed) {
        g_last_interaction_ms=ms;
        g_need_refine=true;
    }
    return changed;
}

static bool mv_buttons(const t5_app_input_t *input,uint32_t now) {
    const uint32_t held=g_controller.held;
    if(input->buttons&T5_APP_BUTTON_BACK || input->exit_requested || (held&MV_PAD_BACK)) return false;
    const uint32_t mapped=mv_controller_filter_mapped(&g_controller,input->buttons,MV_MAPPED_CAMERA_BUTTONS);
    mv_motion_delta_t delta;
    bool changed=mv_motion_step(&g_motion,held,now,g_video->can_submit(),&delta);
    if(changed) {
        g_view.yaw=mv_wrap_angle(g_view.yaw+delta.yaw);
        g_view.pitch=mv_wrap_angle(g_view.pitch+delta.pitch);
        g_view.zoom=mv_clampf(g_view.zoom*delta.zoom_factor,0.18f,7.0f);
        g_view.pan_x=mv_clampf(g_view.pan_x+delta.pan_x,-4096.0f,4096.0f);
        g_view.pan_y=mv_clampf(g_view.pan_y+delta.pan_y,-4096.0f,4096.0f);
    }
    /* Mapped input has no source/bumper information. It is a fallback only,
     * never a second rotation/reset path alongside the raw gamepad. */
    if(mapped&T5_APP_BUTTON_LEFT){g_view.yaw=mv_wrap_angle(g_view.yaw-0.012f);changed=true;}
    if(mapped&T5_APP_BUTTON_RIGHT){g_view.yaw=mv_wrap_angle(g_view.yaw+0.012f);changed=true;}
    if(mapped&T5_APP_BUTTON_UP){g_view.pitch=mv_wrap_angle(g_view.pitch-0.012f);changed=true;}
    if(mapped&T5_APP_BUTTON_DOWN){g_view.pitch=mv_wrap_angle(g_view.pitch+0.012f);changed=true;}
    if(mapped&T5_APP_BUTTON_CONFIRM) {
        mv_reset_view();
        memset(&g_motion,0,sizeof(g_motion));
        changed=true;
    }
    if(changed){g_last_interaction_ms=now;g_need_refine=true;g_draw_pending=true;}
    return true;
}

__attribute__((visibility("default"))) uint32_t app_hardware_takeover(void) {
    return T5_HARDWARE_TAKEOVER_DISPLAY;
}

__attribute__((visibility("default"))) void app_main(void) {
    memset(&g_model,0,sizeof(g_model));
    memset(g_path,0,sizeof(g_path));
    g_shaded=false; g_depth=NULL; g_status[0]=0;
    g_shade_capture=false; g_shade_tap=false;
    g_touch_was_down=false; g_last_tap_ms=0;
    g_render_input_pending=false;
    memset(&g_controller,0,sizeof(g_controller));
    memset(&g_motion,0,sizeof(g_motion));
    g_draw_pending=false;
    g_touch_lease=T5_PROVIDER_CAPABILITY_LEASE_INVALID;

    g_app=t5_app_get_api(T5_APP_ABI_VERSION);
    g_storage=t5_storage_get_api(T5_STORAGE_API_VERSION);
    g_file_open=t5_file_open_get_api(T5_FILE_OPEN_API_VERSION);
    g_video=t5_video_get_api(T5_VIDEO_API_VERSION);
    if(!g_app || !g_storage || !g_file_open || !g_video ||
       g_storage->struct_size<offsetof(t5_storage_api_v1,stream_close)+sizeof(g_storage->stream_close) ||
       !g_storage->stream_open || !g_storage->stream_read || !g_storage->stream_seek || !g_storage->stream_close ||
       g_file_open->struct_size<sizeof(*g_file_open) || !g_file_open->source_path_get ||
       g_video->struct_size<sizeof(*g_video) || !g_video->start || !g_video->backbuffer ||
       !g_video->can_submit || !g_video->submit || !g_video->stop) return;

    const size_t psram_end=offsetof(t5_app_api_v1,psram_free)+sizeof(g_app->psram_free);
    g_use_psram=g_app->struct_size>=psram_end && g_app->psram_alloc && g_app->psram_free;

    if(!g_video->start(&g_surface)) return;
    if(g_surface.width!=960u || g_surface.height!=540u ||
       g_surface.stride_bytes!=120u ||
       g_surface.pixel_format!=T5_VIDEO_PIXEL_MONO_1BPP_MSB ||
       (g_surface.flags&T5_VIDEO_FLAG_ONE_IS_BLACK)==0u) {
        mv_message("UNSUPPORTED VIDEO","EXPECTED 960X540 MONO");
        g_video->stop();
        return;
    }

    if(!g_file_open->source_path_get(g_path,sizeof(g_path))) {
        mv_message("3D MODEL VIEWER","OPEN OBJ OR STL FROM FILE BROWSER");
        for(;;){
            t5_app_input_t input={0};
            if(!g_app->poll(&input,30) || input.exit_requested || (input.buttons&T5_APP_BUTTON_BACK)) break;
        }
        g_video->stop();
        return;
    }

    mv_message("LOADING MODEL",mv_basename(g_path));
    if(!mv_load_model(g_path,&g_model)) {
        mv_message("MODEL LOAD FAILED","OBJ/STL INVALID OR TOO COMPLEX");
        for(;;){
            t5_app_input_t input={0};
            if(!g_app->poll(&input,30) || input.exit_requested || (input.buttons&T5_APP_BUTTON_BACK)) break;
        }
        g_video->stop();
        mv_model_free(&g_model);
        return;
    }

    if(!mv_touch_begin()) {
        mv_message("TOUCH UNAVAILABLE","INSTALL INPUT.TOUCH.RAW DRIVER");
        for(;;){
            t5_app_input_t input={0};
            if(!g_app->poll(&input,30) || input.exit_requested || (input.buttons&T5_APP_BUTTON_BACK)) break;
        }
        g_video->stop();
        mv_model_free(&g_model);
        return;
    }

    mv_controller_open(&g_controller,g_caps);
    mv_reset_view();
    g_last_interaction_ms=g_app->millis();
    g_need_refine=!mv_render(false);

    mv_contacts_t previous={0};
    bool running=true;
    while(running) {
        t5_app_input_t input={0};
        if (g_render_input_pending) {
            input=g_render_input;
            g_render_input_pending=false;
        } else if(!g_app->poll(&input,8)) break;
        (void)mv_controller_poll(&g_controller);
        const uint32_t now=g_app->millis();
        if(!mv_buttons(&input,now)) break;

        mv_contacts_t current={0};
        bool home=false;
        if(mv_touch_snapshot(&current,&home)) {
            if(home) break;
            const bool changed=mv_handle_touch(&previous,&current,now);
            if(changed) g_draw_pending=true;
            previous=current;
        }

        /* Both touch and controller changes present while moving; a busy or
         * aborted frame stays dirty instead of waiting for the idle timer. */
        if(g_draw_pending && mv_render(true)) g_draw_pending=false;

        if(g_need_refine && !g_draw_pending && previous.count==0u &&
           !mv_motion_action(g_controller.held) &&
           (uint32_t)(now-g_last_interaction_ms)>=MV_REFINE_DELAY_MS) {
            if(mv_render(false)) g_need_refine=false;
        }
    }

    mv_controller_close(&g_controller);
    mv_shade_release();
    mv_touch_end();
    g_video->stop();
    mv_model_free(&g_model);
}
