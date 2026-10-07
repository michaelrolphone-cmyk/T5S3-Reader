#include "src/runtime/display/ProviderDisplaySurface.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

struct Fake {
    uint8_t pixels[8]{}, prior[8]{};
    unsigned seedCount=0, damageCount=0;
    risc_display_rect_v1 damage{};
    bool seedFails=false;
    uint32_t acquireCount = 0, releaseCount = 0, submitCount = 0, waitCount = 0;
    uint8_t intent = 0;
    uint32_t timeout = 0;
    bool wrongSurface = false, acquireFails = false, submitFails = false, waitFails = false;
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
    if (fake.acquireFails) return false;
    *out = {1, fake.pixels, 8, 8, 1, fake.wrongSurface ? 7u : 8u, RISC_DISPLAY_FORMAT_MONO1};
    return true;
}
static void release(void*, risc_display_frame_v1 frame) {
    assert(frame == 1);
    ++fake.releaseCount;
}
static bool submit(void*, risc_display_frame_v1 frame, const risc_display_rect_v1* rect,
                   size_t damage, const risc_display_present_options_v1* options,
                   risc_display_present_token_v1* token) {
    assert(frame == 1 && damage <= 1);
    fake.damageCount=damage;if(damage)fake.damage=*rect;
    ++fake.submitCount;
    fake.intent = options->intent;
    if (fake.submitFails) return false;
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
static bool seed(void*, risc_display_frame_v1 frame) {
    assert(frame==1); ++fake.seedCount;
    std::memcpy(fake.prior,fake.pixels,sizeof(fake.prior));return !fake.seedFails;
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
    surface.clearPresentStatus();
    assert(!surface.lastPresentSucceeded() && surface.lastPresentState() == 0);
    assert(fake.pixels[0] == 0x80 && fake.pixels[1] == 0);
    assert(fake.intent == RISC_DISPLAY_PRESENT_CLEAN && fake.timeout == 1234 && fake.releaseCount == 0);
    surface.displayBuffer(DisplayPresentMode::LowLatency);
    assert(surface.lastPresentSucceeded());
    assert(fake.intent == RISC_DISPLAY_PRESENT_LOW_LATENCY && fake.submitCount == 2);
    fake.wrongSurface = true;
    surface.displayBuffer();
    assert(!surface.lastPresentSucceeded() && fake.submitCount == 2 && fake.releaseCount == 1);
    fake.wrongSurface = false;
    fake.waitFails = true;
    surface.displayBuffer();
    assert(!surface.lastPresentSucceeded() && surface.lastPresentState() == RISC_DISPLAY_PRESENT_FAILED);
    assert(fake.releaseCount == 2);

    fake.waitFails = false;
    // Exercise every byte value and an asymmetric complete raster. The expected
    // result is computed pixel-by-pixel, independently from the copy algorithm.
    for (unsigned pattern = 0; pattern < 256; ++pattern) {
        for (size_t row = 0; row < sizeof(raster); ++row)
            raster[row] = static_cast<uint8_t>(pattern + row * 29u);
        uint8_t original[sizeof(raster)];
        std::memcpy(original, raster, sizeof(raster));
        for (bool flipped : {false, true, true, false}) {
            surface.setFlipOutput(flipped);
            surface.displayBuffer(DisplayPresentMode::LowLatency);
            assert(surface.lastPresentSucceeded());
            assert(!std::memcmp(original, raster, sizeof(raster)));
            for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
                const unsigned sx = flipped ? 7 - x : x, sy = flipped ? 7 - y : y;
                const bool expected = !(original[sy] & (0x80u >> sx));
                assert(bool(fake.pixels[y] & (0x80u >> x)) == expected);
            }
        }
    }
    // An unchanged flag does not force another clean refresh. A changed flag
    // cannot lose its clean request to lower-priority requests or failures.
    surface.setFlipOutput(false);
    surface.displayBuffer(DisplayPresentMode::LowLatency);
    assert(fake.intent == RISC_DISPLAY_PRESENT_LOW_LATENCY);
    surface.setFlipOutput(true);
    surface.requestNextRefresh(DisplayPresentMode::LowLatency);
    fake.acquireFails = true;
    surface.displayBuffer(); assert(!surface.lastPresentSucceeded());
    fake.acquireFails = false; fake.wrongSurface = true;
    surface.displayBuffer(); assert(!surface.lastPresentSucceeded());
    fake.wrongSurface = false; fake.submitFails = true;
    surface.displayBuffer(); assert(!surface.lastPresentSucceeded());
    assert(fake.intent == RISC_DISPLAY_PRESENT_CLEAN);
    fake.submitFails = false; fake.waitFails = true;
    surface.displayBuffer(); assert(!surface.lastPresentSucceeded());
    assert(fake.intent == RISC_DISPLAY_PRESENT_CLEAN);
    fake.waitFails = false;
    surface.displayBuffer(DisplayPresentMode::LowLatency);
    assert(surface.lastPresentSucceeded() && fake.intent == RISC_DISPLAY_PRESENT_CLEAN);
    surface.setFlipOutput(true);
    surface.displayBuffer(DisplayPresentMode::LowLatency);
    assert(fake.intent == RISC_DISPLAY_PRESENT_LOW_LATENCY);
    surface.setFlipOutput(false);
    surface.displayBuffer(DisplayPresentMode::LowLatency);
    assert(fake.intent == RISC_DISPLAY_PRESENT_CLEAN);

    const uint32_t submitted = fake.submitCount;
    surface.displayGrayBuffer();
    assert(!surface.lastPresentSucceeded() && fake.submitCount == submitted);
    ProviderDisplaySurface invalid(&api, raster, sizeof(raster) - 1, 8, 8);
    assert(!invalid.isReady() && !invalid.getFrameBuffer());
    risc_display_output_api_v1_history ext{api,RISC_DISPLAY_HISTORY_TAG,1,seed};
    ext.base.struct_size=sizeof(ext);
    ProviderDisplaySurface diff(&ext.base,raster,sizeof(raster),8,8,1234);
    uint8_t previous[8];std::memset(previous,0xFF,sizeof(previous));
    std::memcpy(raster,previous,sizeof(raster));raster[2]=0x7F;
    diff.displayBufferDiff(previous,DisplayPresentMode::Quality);
    assert(diff.lastPresentSucceeded() && fake.seedCount==1 && fake.damageCount==1);
    assert(fake.prior[2]==0 && fake.pixels[2]==0x80);
    assert(fake.damage.x==0 && fake.damage.y==2 && fake.damage.width==8 && fake.damage.height==1);
    diff.setFlipOutput(true);diff.suppressInitialFullRefresh();
    diff.displayBufferDiff(previous,DisplayPresentMode::Quality);
    assert(fake.damage.y==5 && fake.pixels[5]==1 && fake.prior[5]==0);
    fake.seedFails=true;const auto calls=fake.submitCount;
    diff.displayBufferDiff(previous,DisplayPresentMode::Quality);
    assert(!diff.lastPresentSucceeded() && fake.submitCount==calls);
    fake.seedFails=false;std::memcpy(raster,previous,sizeof(raster));
    diff.displayBufferDiff(previous,DisplayPresentMode::Quality);
    assert(diff.lastPresentSucceeded() && fake.submitCount==calls);
    std::puts("provider display surface: old-frame seeding, clipped damage, flip, refusal and unchanged frame PASS");
}
