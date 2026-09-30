#pragma once

/* App-side convenience wrapper for the installable display.output capability.
 * No panel or firmware video interface is visible to renderers. */
#include "RiscDisplayOutputV1.h"
#include "T5ProviderCapabilityApi.h"

#define DISPLAY_CLIENT_MONO1 RISC_DISPLAY_FORMAT_MONO1
#define DISPLAY_CLIENT_GRAY2 RISC_DISPLAY_FORMAT_GRAY2
#define DISPLAY_CLIENT_ONE_IS_BLACK 1u

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
    uint32_t flags;
} display_client_surface;

typedef struct {
    bool (*start)(display_client_surface *surface);
    uint8_t *(*backbuffer)(size_t *size_out);
    bool (*can_submit)(void);
    bool (*submit)(uint16_t dirty_y, uint16_t dirty_height);
    void (*stop)(void);
    bool (*start_format)(display_client_surface *surface, uint8_t format);
} display_client_api;

static const t5_provider_capability_api_v1 *display_host;
static const risc_display_output_api_v1 *display_output;
static t5_provider_capability_lease_t display_lease;
static risc_display_surface_v1 display_frame;
static uint32_t display_format;

static void display_client_stop(void) {
    if (display_output && display_frame.frame)
        display_output->release(display_output->context, display_frame.frame);
    display_frame = (risc_display_surface_v1){0};
    display_output = NULL;
    if (display_host && display_lease) (void)display_host->release(display_lease);
    display_lease = 0;
    display_host = NULL;
}

static bool display_client_start_format(display_client_surface *surface, uint8_t format) {
    if (!surface || display_lease ||
        (format != DISPLAY_CLIENT_MONO1 && format != DISPLAY_CLIENT_GRAY2)) return false;
    display_host = t5_provider_capability_get_api(T5_PROVIDER_CAPABILITY_API_VERSION);
    const void *interface = NULL;
    if (!display_host || display_host->api_version != T5_PROVIDER_CAPABILITY_API_VERSION ||
        display_host->struct_size < sizeof(*display_host) ||
        !display_host->acquire || !display_host->release ||
        !display_host->acquire(RISC_DISPLAY_OUTPUT_CAPABILITY,
                               RISC_DISPLAY_OUTPUT_API_V1, &display_lease, &interface) ||
        !display_lease || !interface) {
        display_client_stop();
        return false;
    }
    display_output = (const risc_display_output_api_v1 *)interface;
    risc_display_info_v1 info = {0};
    if (display_output->api_version != RISC_DISPLAY_OUTPUT_API_V1 ||
        display_output->struct_size < sizeof(*display_output) ||
        !display_output->get_info || !display_output->acquire ||
        !display_output->release || !display_output->submit ||
        !display_output->present_status ||
        !display_output->get_info(display_output->context, &info) ||
        info.api_version != RISC_DISPLAY_OUTPUT_API_V1 ||
        !(info.supported_formats & RISC_DISPLAY_FORMAT_BIT(format))) {
        display_client_stop();
        return false;
    }
    // Negotiate a real surface now: do not fabricate a packed stride from info.
    if (!display_output->acquire(display_output->context, format, &display_frame) ||
        !display_frame.frame || !display_frame.pixels ||
        display_frame.pixel_format != format || !display_frame.width || !display_frame.height ||
        display_frame.width > 4096u || display_frame.height > 4096u ||
        display_frame.stride_bytes < (display_frame.width * (format == DISPLAY_CLIENT_GRAY2 ? 2u : 1u) + 7u) / 8u ||
        display_frame.stride_bytes > display_frame.size_bytes / display_frame.height) {
        display_client_stop();
        return false;
    }
    display_format = format;
    *surface = (display_client_surface){display_frame.width, display_frame.height,
        display_frame.stride_bytes, format, DISPLAY_CLIENT_ONE_IS_BLACK};
    return true;
}

static bool display_client_start(display_client_surface *surface) {
    return display_client_start_format(surface, DISPLAY_CLIENT_MONO1);
}

static uint8_t *display_client_backbuffer(size_t *size_out) {
    if (size_out) *size_out = 0;
    if (!display_output) return NULL;
    if (!display_frame.frame &&
        !display_output->acquire(display_output->context, display_format,
                                 &display_frame)) return NULL;
    if (!display_frame.frame || !display_frame.pixels) return NULL;
    if (size_out) *size_out = display_frame.size_bytes;
    return (uint8_t *)display_frame.pixels;
}

static bool display_client_can_submit(void) {
    if (!display_output) return false;
    if (display_frame.frame) return true;
    size_t bytes = 0;
    return display_client_backbuffer(&bytes) != NULL;
}

static bool display_client_submit(uint16_t dirty_y, uint16_t dirty_height) {
    if (!display_output || !display_frame.frame) return false;
    risc_display_rect_v1 dirty = {0, dirty_y, display_frame.width, dirty_height};
    risc_display_present_token_v1 token = 0;
    if (!display_output->submit(display_output->context, display_frame.frame,
                                dirty_height ? &dirty : NULL, dirty_height ? 1u : 0u,
                                NULL, &token)) return false;
    display_frame = (risc_display_surface_v1){0};
    return true;
}

static const display_client_api *display_client_get_api(void) {
    static const display_client_api api = {
        display_client_start, display_client_backbuffer, display_client_can_submit,
        display_client_submit, display_client_stop, display_client_start_format
    };
    return &api;
}
