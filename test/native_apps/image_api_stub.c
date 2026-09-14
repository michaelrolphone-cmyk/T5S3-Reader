#include "T5ImageApi.h"

#include <stddef.h>
#include <stdint.h>

static bool viewer_open_request(const char *source_path, uint64_t cookie) {
    (void)source_path;
    (void)cookie;
    return false;
}

static bool viewer_open_take_result(int32_t *esp_error, uint64_t *cookie) {
    (void)esp_error;
    (void)cookie;
    return false;
}

static bool source_path_get(char *out, size_t capacity) {
    if (out && capacity) out[0] = 0;
    return false;
}

static bool probe(const char *source_path, t5_image_info_t *out) {
    (void)source_path;
    (void)out;
    return false;
}

static bool render_fit(const char *source_path, int32_t x, int32_t y, int32_t width, int32_t height) {
    (void)source_path;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    return false;
}

static const t5_image_api_v1 api = {
    T5_IMAGE_API_VERSION,
    sizeof(t5_image_api_v1),
    viewer_open_request,
    viewer_open_take_result,
    source_path_get,
    probe,
    render_fit,
};

const t5_image_api_v1 *t5_image_get_api(uint32_t version) {
    return version == T5_IMAGE_API_VERSION ? &api : NULL;
}
