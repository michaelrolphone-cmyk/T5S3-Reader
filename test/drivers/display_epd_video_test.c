#include <assert.h>
#include <string.h>
#include "RiscDisplayOutputV1.h"
#include "RiscProviderV2.h"
#include "T5VideoApi.h"

static unsigned starts, stops, submits;
static bool pending;
static bool incompatible;
static uint32_t frames;
static uint8_t pixels[64800];
static bool start_video(t5_video_surface_v1 *out) {
    ++starts;
    *out = (t5_video_surface_v1){960, 540, 120, T5_VIDEO_PIXEL_MONO_1BPP_MSB,
                                  T5_VIDEO_FLAG_ONE_IS_BLACK};
    if (incompatible) out->width = 1;
    return true;
}
static uint8_t *buffer(size_t *bytes) { *bytes = sizeof(pixels); return pixels; }
static bool can_submit(void) { return !pending; }
static bool submit_video(uint16_t y, uint16_t height) {
    assert(y == 8 && height == 4);
    ++submits;
    pending = true;
    return true;
}
static bool is_pending(void) { return pending; }
static uint32_t frame_counter(void) { return frames; }
static void stop_video(void) { ++stops; pending = false; }
static const t5_video_api_v1 video_api = {
    T5_VIDEO_API_VERSION, sizeof(t5_video_api_v1), start_video, buffer,
    can_submit, submit_video, is_pending, frame_counter, stop_video
};
const t5_video_api_v1 *t5_video_get_api(uint32_t version) {
    return version == T5_VIDEO_API_VERSION ? &video_api : NULL;
}

int main(void) {
    const risc_driver_v2 *driver = t5_driver_get(RISC_PROVIDER_DRIVER_ABI_V2);
    assert(driver && driver->start(NULL, 0) && starts == 0);
    const risc_display_output_api_v1 *output = driver->capability;
    risc_display_info_v1 info;
    assert(output->get_info(NULL, &info) && info.width == 960 && info.height == 540);
    risc_display_surface_v1 frame;
    assert(output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame) && starts == 1);
    assert(frame.pixels == pixels && frame.size_bytes == sizeof(pixels));
    assert(!driver->quiesce());
    const risc_display_rect_v1 dirty = {3, 8, 100, 4};
    risc_display_present_token_v1 token = 0;
    assert(output->submit(NULL, frame.frame, &dirty, 1, NULL, &token) && token && submits == 1);
    assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
    risc_display_present_status_v1 status;
    assert(output->present_status(NULL, token, &status) && status.state == RISC_DISPLAY_PRESENT_QUEUED);
    pending = false; ++frames;
    assert(output->present_status(NULL, token, &status) && status.state == RISC_DISPLAY_PRESENT_COMPLETE);
    assert(driver->quiesce() && stops == 1);
    driver->stop();
    assert(stops == 1);
    assert(driver->start(NULL, 0));
    incompatible = true;
    assert(!output->acquire(NULL, RISC_DISPLAY_FORMAT_MONO1, &frame));
    assert(stops == 2 && driver->quiesce());
}
