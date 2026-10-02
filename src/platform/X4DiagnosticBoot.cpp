#include "X4DiagnosticBoot.h"

#include "RiscDisplayOutputV1.h"
#include "RiscFrontlightV1.h"
#include "RiscInputNavigationV1.h"
#include "RiscPlatformClockV1.h"
#include "x4pro_embedded.h"

#include <Board.h>
#include <Arduino.h>
#include <Logging.h>
#include <cstring>

#include "runtime/drivers/ProviderModuleV2.h"

namespace {
RuntimeProviders::ModuleV2 clock_mod;
RuntimeProviders::ModuleV2 panel_mod;
RuntimeProviders::ModuleV2 buttons_mod;
RuntimeProviders::ModuleV2 light_mod;
const risc_display_output_api_v1 *display_api = nullptr;
const risc_input_navigation_api_v1 *nav_api = nullptr;
const risc_frontlight_api_v1 *light_api = nullptr;
uint8_t surface[48000];
risc_display_frame_v1 frame = 0;
uint32_t sequence = 0;
uint32_t last_buttons = 0;
bool ready = false;

void log_fail(const char *stage, RuntimeProviders::ModuleV2 &mod) {
    LOG_ERR("X4", "%s failed: %s", stage, mod.lastError() ? mod.lastError() : "unknown");
}
bool load_one(RuntimeProviders::ModuleV2 &mod, const x4_embedded_provider &provider,
              const risc_provider_dependency_v1 *deps, size_t count) {
    static const char *none[] = {""};
    const char *const *imports = provider.imports ? provider.imports : none;
    if (!mod.loadVerifiedBytes(provider.bytes, provider.size, provider.sha256, imports,
                               provider.import_count, provider.id, provider.capability,
                               provider.api, deps, count)) {
        log_fail(provider.id, mod);
        return false;
    }
    LOG_INF("X4", "loaded %s %s", provider.id, provider.version);
    return true;
}
void pixel(int x, int y, bool black) {
    if (x < 0 || y < 0 || x >= 800 || y >= 480) return;
    uint8_t &byte = surface[(size_t)y * 100u + (size_t)x / 8u];
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    if (black) byte |= mask;
    else byte &= (uint8_t)~mask;
}
void marker(int x, int y) {
    for (int i = 0; i < 24; ++i) { pixel(x + i, y, true); pixel(x, y + i, true); }
}
void paint(uint32_t edge) {
    memset(surface, 0x00, sizeof(surface));
    for (int i = 0; i < 800; ++i) { pixel(i, 0, true); pixel(i, 479, true); }
    for (int i = 0; i < 480; ++i) { pixel(0, i, true); pixel(799, i, true); }
    marker(8, 8);
    marker(760, 8);
    marker(8, 440);
    for (int x = 40; x < 200; ++x) pixel(x, 40, true);
    for (int y = 40; y < 120; ++y) pixel(40, y, true);
    for (uint32_t n = 0; n < (sequence & 7u); ++n) marker(80 + (int)n * 28, 200);
    if (edge & RISC_NAV_LEFT) marker(80, 300);
    if (edge & RISC_NAV_RIGHT) marker(160, 300);
    if (edge & RISC_NAV_CONFIRM) marker(240, 300);
}
bool present() {
    risc_display_surface_v1 out{};
    if (!display_api->acquire(display_api->context, RISC_DISPLAY_FORMAT_MONO1, &out)) {
        LOG_ERR("X4", "acquire failed");
        return false;
    }
    if (out.width != 800 || out.height != 480 || out.stride_bytes != 100 ||
        out.size_bytes != 48000 || out.pixel_format != RISC_DISPLAY_FORMAT_MONO1 || !out.pixels) {
        LOG_ERR("X4", "surface geometry rejected");
        display_api->release(display_api->context, out.frame);
        return false;
    }
    memcpy(out.pixels, surface, sizeof(surface));
    frame = out.frame;
    risc_display_present_token_v1 token = 0;
    if (!display_api->submit(display_api->context, frame, nullptr, 0, nullptr, &token)) {
        LOG_ERR("X4", "submit failed");
        return false;
    }
    risc_display_present_status_v1 status{};
    if (!display_api->wait_present(display_api->context, token, 20000, &status) ||
        status.state != RISC_DISPLAY_PRESENT_COMPLETE) {
        LOG_ERR("X4", "present did not complete state=%u", status.state);
        return false;
    }
    LOG_INF("X4", "present complete sequence=%lu", static_cast<unsigned long>(sequence));
    return true;
}
}

void x4DiagnosticSetup() {
    Serial.begin(115200);
    const unsigned long start = millis();
    while (!Serial && millis() - start < 200) delay(10);
    LOG_INF("X4", "diagnostic boot %s flash=16MB app0=0x10000", Board::firmwareMarker());
    const x4_embedded_provider *clock = x4_embedded_find("platform-clock-v1");
    const x4_embedded_provider *panel = x4_embedded_find("x4pro-panel");
    const x4_embedded_provider *buttons = x4_embedded_find("x4pro-buttons");
    const x4_embedded_provider *light = x4_embedded_find("x4pro-frontlight");
    if (!clock || !panel || !buttons || !light) {
        LOG_ERR("X4", "embedded provider missing");
        return;
    }
    if (!load_one(clock_mod, *clock, nullptr, 0)) return;
    risc_provider_dependency_v1 dep{clock->capability, 1, clock_mod.capability()};
    if (!load_one(panel_mod, *panel, &dep, 1)) return;
    if (!load_one(buttons_mod, *buttons, nullptr, 0)) return;
    if (!load_one(light_mod, *light, nullptr, 0)) return;
    display_api = static_cast<const risc_display_output_api_v1 *>(panel_mod.capability());
    nav_api = static_cast<const risc_input_navigation_api_v1 *>(buttons_mod.capability());
    light_api = static_cast<const risc_frontlight_api_v1 *>(light_mod.capability());
    risc_display_info_v1 info{};
    if (!display_api || !display_api->get_info(display_api->context, &info) ||
        info.width != 800 || info.height != 480 ||
        info.preferred_format != RISC_DISPLAY_FORMAT_MONO1) {
        LOG_ERR("X4", "display.output geometry rejected");
        return;
    }
    if (light_api) (void)light_api->set_level(light_api->context, 0, 1);
    paint(0);
    ready = present();
}

void x4DiagnosticLoop() {
    if (!ready || !nav_api) {
        delay(200);
        return;
    }
    risc_input_navigation_frame_v1 frame_in{};
    if (!nav_api->poll(nav_api->context, &frame_in)) {
        delay(20);
        return;
    }
    if (frame_in.pressed) {
        last_buttons = frame_in.pressed;
        ++sequence;
        paint(frame_in.pressed);
        if (!present()) ready = false;
    }
    delay(20);
}
