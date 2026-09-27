#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T5_VIDEO_API_VERSION 1u
#define T5_VIDEO_PIXEL_MONO_1BPP_MSB 1u
#define T5_VIDEO_FLAG_ONE_IS_BLACK (1u << 0)

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride_bytes;
    uint8_t pixel_format;
    uint8_t flags;
} t5_video_surface_v1;

typedef struct {
    uint32_t api_version;
    uint32_t struct_size;

    /* Start the fast hardware-takeover scan engine. The calling ELF must
     * export app_hardware_takeover() with T5_HARDWARE_TAKEOVER_DISPLAY.
     * On success, surface describes the physical scan-oriented framebuffer. */
    bool (*start)(t5_video_surface_v1 *surface);

    /* Returns the writable backbuffer. It remains owned by the video service;
     * submit() atomically swaps it at a scan boundary. */
    uint8_t *(*backbuffer)(size_t *size_out);

    /* Non-blocking frame publication. dirty_y/dirty_height are physical scan
     * rows; height==0 means the full panel. */
    bool (*can_submit)(void);
    bool (*submit)(uint16_t dirty_y, uint16_t dirty_height);
    bool (*pending)(void);
    uint32_t (*frame_counter)(void);

    /* Stop every scan task, DMA callback and panel resource before the ELF
     * returns. The host also force-stops this service before restoring its
     * normal display as a final unload guard. */
    void (*stop)(void);
} t5_video_api_v1;

const t5_video_api_v1 *t5_video_get_api(uint32_t api_version);

#ifdef __cplusplus
}
#endif
