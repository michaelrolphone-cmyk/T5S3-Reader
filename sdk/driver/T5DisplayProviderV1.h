#pragma once
/* Transitional T5 display extension. The base remains the ordinary
 * display.output v1 ABI for portable consumers. Reader's existing grayscale
 * waveform/history operations and the legacy fast-video API share ONE physical
 * provider. These are ELF implementations, never firmware peripheral imports.
 */
#include "RiscDisplayOutputV1.h"
typedef struct t5_video_api_v1 t5_video_api_v1;
#ifdef __cplusplus
extern "C" {
#endif
#define T5_DISPLAY_EXTENSION_TAG 0x54354431u
#define T5_DISPLAY_EXTENSION_VERSION 1u

typedef struct {
    uint32_t api_version,struct_size;
    bool (*start)(bool clear_panel);
    /* Physical scan orientation, grayscale 0=black,255=white. The full canvas
     * is 960x540 with stride960. Only the validated rectangle is copied into
     * provider-owned history before return; no retained caller pointer.
     * mode uses the existing M5 EPD mode ordinal, validated by this provider. */
    bool (*write_gray)(const uint8_t *canvas,size_t bytes,
                       uint16_t x,uint16_t y,uint16_t width,uint16_t height,uint8_t mode);
    bool (*wait)(uint32_t timeout_ms);
    bool (*set_power)(bool on);
    bool (*suppress_output)(bool suppressed);
    /* False retains worker/DMA/IRQ/power state and blocks any other engine. */
    bool (*try_stop)(void);
} t5_display_quality_api_v1;

typedef struct {
    risc_display_output_api_v1 base;
    uint32_t extension_tag,extension_version;
    const t5_display_quality_api_v1 *quality;
    const t5_video_api_v1 *fast;
} t5_display_provider_api_v1;
#ifdef __cplusplus
}
#endif
