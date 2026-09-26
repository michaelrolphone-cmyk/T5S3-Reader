#include "T5AppApi.h"
#include "T5FileOpenApi.h"
#include "T5ImageApi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PATH_CAP T5_IMAGE_PATH_MAX

static const t5_app_api_v1 *app;
static const t5_image_api_v1 *image;
static const t5_file_open_api_v1 *file_open;

static const char *format_name(uint32_t format) {
    switch (format) {
        case T5_IMAGE_FORMAT_JPEG: return "JPEG";
        case T5_IMAGE_FORMAT_PNG: return "PNG";
        case T5_IMAGE_FORMAT_BMP: return "BMP";
        default: return "Image";
    }
}

static const char *basename_of(const char *path) {
    const char *slash = path ? strrchr(path, '/') : NULL;
    return slash ? slash + 1 : path;
}

static void draw_message(const char *title, const char *message) {
    app->clear();
    app->draw_text(24, 28, title ? title : "Image Viewer");
    if (message) app->draw_text(24, 96, message);
    app->draw_text(24, app->screen_height() - 52, "Back: Close");
    app->present(true);
}

void app_main(void) {
    char path[PATH_CAP] = {0};
    t5_image_info_t info = {0};
    char meta[96];

    app = t5_app_get_api(T5_APP_ABI_VERSION);
    image = t5_image_get_api(T5_IMAGE_API_VERSION);
    file_open = t5_file_open_get_api(T5_FILE_OPEN_API_VERSION);
    if (!app || !image || image->struct_size < sizeof(*image) ||
        !image->probe || !image->render_fit ||
        !file_open || file_open->struct_size < sizeof(*file_open) ||
        !file_open->source_path_get) return;

    if (!file_open->source_path_get(path, sizeof(path))) {
        draw_message("Image Viewer", "Open a JPG, PNG, or BMP from File Browser.");
    } else if (!image->probe(path, &info)) {
        draw_message("Image Viewer", "Unable to read this image.");
    } else {
        const int32_t screen_w = app->screen_width();
        const int32_t screen_h = app->screen_height();
        const int32_t image_top = 72;
        const int32_t image_bottom = screen_h - 72;
        const char *name = basename_of(path);
        app->clear();
        app->draw_text(20, 20, name && name[0] ? name : "Image Viewer");

        /*
         * probe() is safe for PNG, but the current firmware render_fit() PNG
         * path can fault inside PNGdec on real-world files. Do not enter the
         * known-crashing decoder from the ELF app. JPEG/BMP remain unchanged.
         * The host-side decoder is fixed separately in firmware.
         */
        if (info.format == T5_IMAGE_FORMAT_PNG) {
            app->draw_text(24, 112, "PNG decoder error prevented.");
            app->draw_text(24, 148, "Update firmware for PNG rendering.");
        } else if (!image->render_fit(path, 12, image_top, screen_w - 24, image_bottom - image_top)) {
            app->draw_text(24, 112, "Image decode failed.");
        }

        snprintf(meta, sizeof(meta), "%s  %lux%lu", format_name(info.format),
                 (unsigned long)info.width, (unsigned long)info.height);
        app->draw_text(20, screen_h - 48, meta);
        app->present(true);
    }

    t5_app_input_t input;
    while (app->poll(&input, 30)) {
        if (input.exit_requested || (input.buttons & T5_APP_BUTTON_BACK) || input.tapped) return;
    }
}
