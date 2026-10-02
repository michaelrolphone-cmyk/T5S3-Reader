#include "src/runtime/display/ProviderDisplaySurface.h"
#include <cassert>
#include <cstdio>
#include <cstring>

struct Fake {
    uint8_t pixels[8]{};
    uint32_t acquireCount = 0, releaseCount = 0, submitCount = 0, waitCount = 0;
    uint8_t intent = 0;
    uint32_t timeout = 0;
    bool wrongSurface = false, waitFails = false;
} fake;
static bool info(void*, risc_display_info_v1* out) {
    *out = {};
    out->api_version = RISC_DISPLAY_OUTPUT_API_V1;
    out->struct_size = sizeof(*out);
    out->width = out->height = 8;
    out->supported_formats = RISC_DISPLAY_FORMAT_BIT(RISC_DISPLAY_FORMAT_MONO1);
    out->preferred_format = RISC_DISPLAY_FORMAT_MONO1;
    return true;
}
static bool acquire(void*, uint32_t format, risc_display_surface_v1* out) {
    ++fake.acquireCount;
    assert(format == RISC_DISPLAY_FORMAT_MONO1);
    *out = {1, fake.pixels, 8, 8, 1, fake.wrongSurface ? 7u : 8u, RISC_DISPLAY_FORMAT_MONO1};
    return true;
}
static void release(void*, risc_display_frame_v1 frame) {
    assert(frame == 1);
    ++fake.releaseCount;
}
static bool submit(void*, risc_display_frame_v1 frame, const risc_display_rect_v1*,
                   size_t damage, const risc_display_present_options_v1* options,
                   risc_display_present_token_v1* token) {
    assert(frame == 1 && damage == 0);
    ++fake.submitCount;
    fake.intent = options->intent;
    *token = 2;
    return true;
}
static bool wait_present(void*, risc_display_present_token_v1 token, uint32_t timeout,
                         risc_display_present_status_v1* out) {
    assert(token == 2);
    ++fake.waitCount;
    fake.timeout = timeout;
    out->state = fake.waitFails ? RISC_DISPLAY_PRESENT_FAILED : RISC_DISPLAY_PRESENT_COMPLETE;
    return !fake.waitFails;
}
int main() {
    risc_display_output_api_v1 api{};
    api.api_version = RISC_DISPLAY_OUTPUT_API_V1;
    api.struct_size = sizeof(api);
    api.get_info = info;
    api.acquire = acquire;
    api.release = release;
    api.submit = submit;
    api.wait_present = wait_present;
    uint8_t raster[8]{};
    ProviderDisplaySurface surface(&api, raster, sizeof(raster), 8, 8, 1234);
    assert(surface.isReady() && surface.getFrameBuffer() == raster);
    surface.clearScreen();
    assert(raster[0] == 0xff && raster[7] == 0xff);
    raster[0] = 0x7f; // Reader MSB 0 means black.
    surface.requestNextRefresh(DisplayPresentMode::Clean);
    surface.displayBuffer(DisplayPresentMode::LowLatency);
    assert(surface.lastPresentSucceeded() && surface.lastPresentState() == RISC_DISPLAY_PRESENT_COMPLETE);
    assert(fake.pixels[0] == 0x80 && fake.pixels[1] == 0);
    assert(fake.intent == RISC_DISPLAY_PRESENT_CLEAN && fake.timeout == 1234 && fake.releaseCount == 0);
    surface.displayBuffer(DisplayPresentMode::LowLatency);
    assert(fake.intent == RISC_DISPLAY_PRESENT_LOW_LATENCY && fake.submitCount == 2);
    fake.wrongSurface = true;
    surface.displayBuffer();
    assert(!surface.lastPresentSucceeded() && fake.submitCount == 2 && fake.releaseCount == 1);
    fake.wrongSurface = false;
    fake.waitFails = true;
    surface.displayBuffer();
    assert(!surface.lastPresentSucceeded() && surface.lastPresentState() == RISC_DISPLAY_PRESENT_FAILED);
    assert(fake.releaseCount == 2);
    const uint32_t submitted = fake.submitCount;
    surface.displayGrayBuffer();
    assert(!surface.lastPresentSucceeded() && fake.submitCount == submitted);
    ProviderDisplaySurface invalid(&api, raster, sizeof(raster) - 1, 8, 8);
    assert(!invalid.isReady() && !invalid.getFrameBuffer());
    std::puts("provider display surface: PASS");
}
