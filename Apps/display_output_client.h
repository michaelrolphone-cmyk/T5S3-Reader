#pragma once

/* App-side convenience wrapper for the installable display.output capability.
 * No panel or firmware video interface is visible to renderers. */
#include "RiscDisplayOutputV1.h"
#include "T5ProviderCapabilityApi.h"

#define DISPLAY_CLIENT_MONO1 1u
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
} display_client_api;

static const t5_provider_capability_api_v1 *display_host;
static const risc_display_output_api_v1 *display_output;
static t5_provider_capability_lease_t display_lease;
static risc_display_surface_v1 display_frame;

static void display_client_stop(void) {
    if (display_output && display_frame.frame)
        display_output->release(display_output->context, display_frame.frame);
    display_frame = (risc_display_surface_v1){0};
    display_output = NULL;
    if (display_host && display_lease) (void)display_host->release(display_lease);
    display_lease = 0;
    display_host = NULL;
}

static bool display_client_start(display_client_surface *surface) {
    if (!surface || display_lease) return false;
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
        !(info.supported_formats & RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1))) {
        display_client_stop();
        return false;
    }
    *surface = (display_client_surface){info.width, info.height,
        (info.width + 7u) / 8u, DISPLAY_CLIENT_MONO1, DISPLAY_CLIENT_ONE_IS_BLACK};
    return true;
}

static uint8_t *display_client_backbuffer(size_t *size_out) {
    if (size_out) *size_out = 0;
    if (!display_output) return NULL;
    if (!display_frame.frame &&
        !display_output->acquire(display_output->context, RISC_DISPLAY_FORMAT_MONO1,
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
        display_client_submit, display_client_stop
    };
    return &api;
}
