#include "X4DiagnosticBoot.h"

#if defined(BOARD_XTEINK_X4_PRO)

#include "RiscDisplayOutputV1.h"
#include "RiscFrontlightV1.h"
#include "RiscInputNavigationV1.h"
#include "RiscPlatformClockV1.h"
#include "RiscStorageVolumeV1.h"
#include "x4pro_embedded.h"
#include "fontIds.h"
#include "runtime/display/ProviderDisplaySurface.h"

#include <Board.h>
#include <Arduino.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "MappedInputManager.h"
#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "components/UITheme.h"
#include "native/NativeNavigationInput.h"
#include "runtime/drivers/ProviderModuleV2.h"

extern EpdFont notoserif14RegularFont;
extern EpdFontFamily ui10FontFamily;
extern EpdFontFamily ui12FontFamily;
extern EpdFontFamily smallFontFamily;
extern FontDecompressor fontDecompressor;
extern GfxRenderer renderer;
extern FontCacheManager fontCacheManager;
extern MappedInputManager mappedInputManager;
extern ActivityManager activityManager;

namespace {
RuntimeProviders::ModuleV2 clock_mod;
RuntimeProviders::ModuleV2 panel_mod;
RuntimeProviders::ModuleV2 buttons_mod;
RuntimeProviders::ModuleV2 light_mod;
RuntimeProviders::ModuleV2 sd_mod;
const risc_display_output_api_v1 *display_api = nullptr;
const risc_input_navigation_api_v1 *nav_api = nullptr;
const risc_frontlight_api_v1 *light_api = nullptr;
uint8_t surface[48000];
std::unique_ptr<ProviderDisplaySurface> provider_surface;
uint32_t sequence = 0;
bool ready = false;
bool shared_text_ready = false;
const char *storage_status = "SD provider unavailable";
bool storage_mounted = false;
bool showing_home = false;
std::string storage_preview;
std::unique_ptr<Txt> preview_text;
std::string preview_name;
uint64_t preview_position = 0;

void advancePreview() {
    if (!preview_text) return;
    if (preview_position >= preview_text->getFileSize()) {
        storage_preview = "End of " + preview_name;
        return;
    }
    char sample[49] = {0};
    const size_t left = preview_text->getFileSize() - preview_position;
    const size_t count = left < sizeof(sample) - 1u ? left : sizeof(sample) - 1u;
    if (!preview_text->readContent(reinterpret_cast<uint8_t *>(sample),
                                   preview_position, count)) {
        storage_preview = "SD read failed";
        preview_text.reset();
        return;
    }
    for (size_t j = 0; j < count; ++j)
        if ((unsigned char)sample[j] < 32 || (unsigned char)sample[j] > 126) sample[j] = ' ';
    storage_preview = preview_name + ": " + std::string(sample, count);
    preview_position += count;
    LOG_INF("X4", "storage preview bytes=%lu", static_cast<unsigned long>(preview_position));
}
bool openFirstText(const risc_storage_volume_api_v1 *volume, const char *directory) {
    const risc_storage_dir_t cursor = volume->dir_open(volume->context, directory);
    if (cursor == RISC_STORAGE_DIR_INVALID) return false;
    risc_storage_dirent_v1 entry{};
    bool found = false;
    std::string selected;
    std::string selected_name;
    for (unsigned i = 0; i < 16 && volume->dir_next(volume->context, cursor, &entry); ++i) {
        const char *extension = std::strrchr(entry.name, '.');
        if (entry.is_directory || !extension || std::strcmp(extension, ".TXT") != 0) continue;
        char path[RISC_STORAGE_VOLUME_NAME_MAX + 16] = {0};
        const size_t prefix = std::strlen(directory);
        const size_t name = std::strlen(entry.name);
        if (prefix + name + 2 >= sizeof(path)) break;
        std::memcpy(path, directory, prefix);
        size_t at = prefix;
        if (at == 0 || path[at - 1] != '/') path[at++] = '/';
        std::memcpy(path + at, entry.name, name + 1);
        selected = path;
        selected_name = entry.name;
        found = true;
        break;
    }
    volume->dir_close(volume->context, cursor);
    if (!found) return false;
    auto candidate = std::make_unique<Txt>(selected, "/.crosspoint");
    if (!candidate->load() || candidate->getFileSize() > 131072u) {
        storage_preview = "TXT unavailable or too large";
        return true;
    }
    preview_text = std::move(candidate);
    preview_name = std::move(selected_name);
    preview_position = 0;
    advancePreview();
    return true;
}

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
    if (black) byte &= (uint8_t)~mask;
    else byte |= mask;
}
void marker(int x, int y) {
    for (int i = 0; i < 24; ++i) { pixel(x + i, y, true); pixel(x, y + i, true); }
}
void rectangle(int left, int top, int right, int bottom, bool black) {
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) pixel(x, y, black);
}
void paint(uint32_t edge) {
    renderer.clearScreen(0xFF);
    for (int i = 0; i < 800; ++i) { pixel(i, 0, true); pixel(i, 479, true); }
    for (int i = 0; i < 480; ++i) { pixel(0, i, true); pixel(799, i, true); }
    marker(8, 8);
    marker(760, 8);
    marker(8, 440);
    for (int x = 40; x < 200; ++x) pixel(x, 40, true);
    for (int y = 40; y < 120; ++y) pixel(40, y, true);
    /* Large high-contrast targets make a real first frame unambiguous by eye. */
    rectangle(80, 80, 400, 320, true);
    rectangle(180, 160, 300, 240, false);
    rectangle(520, 120, 680, 420, true);
    for (uint32_t n = 0; n < (sequence & 7u); ++n) marker(80 + (int)n * 28, 200);
    if (edge & RISC_NAV_LEFT) marker(80, 300);
    if (edge & RISC_NAV_RIGHT) marker(160, 300);
    if (edge & RISC_NAV_CONFIRM) marker(240, 300);
    /* Exercise the same 0=black software raster and text renderer as Reader UI. */
    if (shared_text_ready) {
        renderer.drawText(NOTOSERIF_14_FONT_ID, 48, 12, "RiscRTE X4 Pro");
        renderer.drawText(NOTOSERIF_14_FONT_ID, 48, 36, storage_status);
    }
}
bool present() {
    uint32_t black_pixels = 0;
    for (uint8_t value : surface) black_pixels += (uint32_t)__builtin_popcount((unsigned)((uint8_t)~value));
    LOG_INF("X4", "diagnostic black_pixels=%lu", static_cast<unsigned long>(black_pixels));
    if (!provider_surface || !provider_surface->isReady()) return false;
    const unsigned long began = millis();
    renderer.displayBuffer(DisplayPresentMode::Clean);
    char detail[160] = "unavailable";
    (void)panel_mod.copyProviderError(detail, sizeof(detail));
    if (!provider_surface->lastPresentSucceeded()) {
        LOG_ERR("X4", "present failed elapsed=%lu budget=20000 %s", millis() - began, detail);
        return false;
    }
    LOG_INF("X4", "present complete elapsed=%lu %s", millis() - began, detail);
    return true;
}
}

void x4DiagnosticSetup() {
    LOG_INF("X4", "diagnostic entered");
    const unsigned long start = millis();
    LOG_INF("X4", "diagnostic boot %s flash=16MB app0=0x10000", Board::firmwareMarker());
    const x4_embedded_provider *clock = x4_embedded_find("platform-clock-v1");
    const x4_embedded_provider *panel = x4_embedded_find("x4pro-panel");
    const x4_embedded_provider *buttons = x4_embedded_find("x4pro-buttons");
    const x4_embedded_provider *light = x4_embedded_find("x4pro-frontlight");
    const x4_embedded_provider *sd = x4_embedded_find("x4pro-sd");
    if (!clock || !panel || !buttons || !light) {
        LOG_ERR("X4", "embedded provider missing");
        return;
    }
    if (!load_one(clock_mod, *clock, nullptr, 0)) return;
    risc_provider_dependency_v1 dep{clock->capability, 1, clock_mod.capability()};
    if (!load_one(panel_mod, *panel, &dep, 1)) return;
    if (!load_one(buttons_mod, *buttons, nullptr, 0)) return;
    if (!load_one(light_mod, *light, nullptr, 0)) return;
    if (sd && load_one(sd_mod, *sd, &dep, 1)) {
        auto *volume = static_cast<const risc_storage_volume_api_v1 *>(sd_mod.capability());
        if (volume && volume->api_version == RISC_STORAGE_VOLUME_API_V1 &&
            volume->struct_size >= sizeof(*volume) && volume->ready) {
            storage_mounted = Storage.bindVolume(volume);
            char reason[80] = "none";
            if (volume->last_error) (void)volume->last_error(volume->context, reason, sizeof(reason));
            storage_status = storage_mounted ? "SD root read only" : "SD card not ready";
            LOG_INF("X4", "storage.volume mounted=%d reason=%s", storage_mounted ? 1 : 0, reason);
            if (storage_mounted) storage_preview = "SD root read only";
            if (storage_mounted && volume->dir_open && volume->dir_next &&
                volume->dir_close && volume->file_open_read && volume->file_read && volume->file_close) {
                storage_preview = "SD ready; no root TXT file";
                if (!openFirstText(volume, "/Books")) (void)openFirstText(volume, "/");
            }
        }
    }
    display_api = static_cast<const risc_display_output_api_v1 *>(panel_mod.capability());
    nav_api = static_cast<const risc_input_navigation_api_v1 *>(buttons_mod.capability());
    if (!nativeNavigationAttachBootstrap(nav_api)) {
        LOG_ERR("X4", "input.navigation bootstrap handoff rejected");
        nav_api = nullptr;
    }
    light_api = static_cast<const risc_frontlight_api_v1 *>(light_mod.capability());
    risc_display_info_v1 info{};
    if (!display_api || !display_api->get_info(display_api->context, &info) ||
        info.width != 800 || info.height != 480 ||
        info.preferred_format != RISC_DISPLAY_FORMAT_MONO1) {
        LOG_ERR("X4", "display.output geometry rejected");
        return;
    }
    provider_surface.reset(new (std::nothrow) ProviderDisplaySurface(display_api, surface,
                           sizeof(surface), 480, 800, 20000));
    if (!provider_surface || !provider_surface->isReady()) {
        LOG_ERR("X4", "display.output surface adapter rejected");
        return;
    }
    if (!display.attachProvider(*provider_surface)) {
        LOG_ERR("X4", "Reader display facade rejected provider");
        return;
    }
    display.begin(false);
    if (!renderer.preflightSurface() || !renderer.begin()) {
        LOG_ERR("X4", "shared renderer surface rejected");
        return;
    }
    renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    if (light_api) (void)light_api->set_level(light_api->context, 0, 1);
    shared_text_ready = fontDecompressor.init();
    if (shared_text_ready) {
        fontCacheManager.setFontDecompressor(&fontDecompressor);
        renderer.setFontCacheManager(&fontCacheManager);
        renderer.insertFont(NOTOSERIF_14_FONT_ID, EpdFontFamily(&notoserif14RegularFont));
        renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
        renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
        renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
    }
    LOG_INF("X4", "shared text ready=%d", shared_text_ready ? 1 : 0);
    char probe[160] = "unavailable";
    (void)panel_mod.copyProviderError(probe, sizeof(probe));
    LOG_INF("X4", "%s", probe);
    if (shared_text_ready) {
        // Render the real Reader Home with a safe fixed Classic/MONO1 frame.
        // Other activities and device-dependent actions remain gated until
        // their X4 power, storage-write and input lifecycles are cut over.
        SETTINGS.uiTheme = CrossPointSettings::CLASSIC;
        UITheme::getInstance().reload();
        activityManager.begin();
        activityManager.goHome();
        // HomeActivity::onEnter queues a deferred update. X4 does not run the
        // normal activity loop yet, so wake its render worker for this frame.
        activityManager.requestUpdate(true);
        showing_home = true;
        LOG_INF("X4", "home activity scheduled=1");
    } else {
        paint(0);
        ready = present();
    }
}

void x4DiagnosticLoop() {
    static unsigned long last = 0;
    if (showing_home && !ready && provider_surface && provider_surface->lastPresentSucceeded()) {
        ready = true;
        LOG_INF("X4", "home present=1");
    }
    const unsigned long now = millis();
    if (now - last >= 2000) {
        last = now;
        LOG_INF("X4", "heartbeat ready=%d", ready ? 1 : 0);
    }
    if (!ready || !nav_api) {
        delay(200);
        return;
    }
    nativeNavigationTick();
    const risc_input_navigation_frame_v1 frame_in = nativeNavigationFrame();
    if (showing_home) {
        static uint32_t input_sequence = 0;
        constexpr struct { uint32_t bit; const char *name; } events[] = {
            {RISC_NAV_LEFT, "left"}, {RISC_NAV_RIGHT, "right"},
            {RISC_NAV_CONFIRM, "confirm"}, {RISC_NAV_BACK, "back"}
        };
        for (const auto &event : events) {
            if (frame_in.pressed & event.bit)
                LOG_INF("X4", "input.navigation event=%s sequence=%lu", event.name,
                        static_cast<unsigned long>(++input_sequence));
        }
        (void)activityManager.dispatchX4Navigation(frame_in.pressed, frame_in.released);
    } else if (frame_in.pressed) {
        ++sequence;
        paint(frame_in.pressed);
        if (!present()) ready = false;
    }
    delay(20);
}

#endif  // BOARD_XTEINK_X4_PRO
