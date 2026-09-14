#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_IMAGE_API_VERSION 1
#define T5_IMAGE_PATH_MAX 512

typedef enum {
    T5_IMAGE_FORMAT_UNKNOWN = 0,
    T5_IMAGE_FORMAT_JPEG = 1,
    T5_IMAGE_FORMAT_PNG = 2,
    T5_IMAGE_FORMAT_BMP = 3,
} t5_image_format_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t format;
} t5_image_info_t;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    // Opens the standard Image Viewer with an absolute SD VFS path, e.g.
    // /sd/photos/example.jpg. The caller is relaunched after the viewer exits.
    bool (*viewer_open_request)(const char *source_path, uint64_t cookie);
    bool (*viewer_open_take_result)(int32_t *esp_error, uint64_t *cookie);

    // Returns the path supplied to the currently running Image Viewer.
    bool (*source_path_get)(char *out, size_t capacity);

    // Shared firmware decode/render services. render_fit preserves aspect ratio,
    // never upscales, and draws into the requested rectangle without presenting.
    bool (*probe)(const char *source_path, t5_image_info_t *out);
    bool (*render_fit)(const char *source_path, int32_t x, int32_t y,
                       int32_t width, int32_t height);
} t5_image_api_v1;

const t5_image_api_v1 *t5_image_get_api(uint32_t version);

#ifdef __cplusplus
}
#endif
