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
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <RiscI2cBusV1.h>
#include <RiscTouchV1.h>
#include <cstring>
#include <esp_heap_caps.h>
#include "runtime/network/PsramTlsAllocator.h"
#include <memory>
#include <new>

#include "MappedInputManager.h"
#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "native/NativeNavigationInput.h"
#include "native/NativeTouchInput.h"
#include "util/ButtonNavigator.h"
#include "runtime/drivers/ProviderModuleV2.h"

extern bool setupDisplayAndFonts();
extern void setupReaderState();
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
extern ActivityManager activityManager;

namespace {
RuntimeProviders::ModuleV2 clock_mod;
RuntimeProviders::ModuleV2 panel_mod;
RuntimeProviders::ModuleV2 buttons_mod;
RuntimeProviders::ModuleV2 light_mod;
RuntimeProviders::ModuleV2 sd_mod;
RuntimeProviders::ModuleV2 i2c_mod;
RuntimeProviders::ModuleV2 touch_mod;
const risc_display_output_api_v1 *display_api = nullptr;
const risc_input_navigation_api_v1 *nav_api = nullptr;
const risc_frontlight_api_v1 *light_api = nullptr;
uint8_t surface[48000];
std::unique_ptr<ProviderDisplaySurface> provider_surface;
bool ready = false;
bool showing_home = false;

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
}

void x4DiagnosticSetup() {
    LOG_INF("X4", "diagnostic entered");
    // Match the shared firmware allocation policy before UI/app allocations.
    if (psramFound()) heap_caps_malloc_extmem_enable(1024);
    RuntimeNetwork::enablePsramTlsAllocations();
    LOG_INF("X4", "diagnostic boot %s flash=16MB app0=0x10000", Board::firmwareMarker());
    const x4_embedded_provider *clock = x4_embedded_find("platform-clock-v1");
    const x4_embedded_provider *panel = x4_embedded_find("x4pro-panel");
    const x4_embedded_provider *buttons = x4_embedded_find("x4pro-buttons");
    const x4_embedded_provider *light = x4_embedded_find("x4pro-frontlight");
    const x4_embedded_provider *sd = x4_embedded_find("x4pro-sd");
    const x4_embedded_provider *i2c = x4_embedded_find("x4pro-i2c");
    const x4_embedded_provider *touch = x4_embedded_find("x4pro-gt911");
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
            const bool storage_mounted = Storage.bindVolume(volume);
            char reason[80] = "none";
            if (volume->last_error) (void)volume->last_error(volume->context, reason, sizeof(reason));
            LOG_INF("X4", "storage.volume mounted=%d reason=%s", storage_mounted ? 1 : 0, reason);

        }
    }
    if (i2c && touch && load_one(i2c_mod, *i2c, &dep, 1)) {
        const risc_provider_dependency_v1 touch_deps[] = {
            {"i2c.bus", 1, i2c_mod.capability()},
            {"platform.clock", 1, clock_mod.capability()}
        };
        if (load_one(touch_mod, *touch, touch_deps, 2)) {
            const auto *touch_api = static_cast<const risc_touch_api_v1 *>(touch_mod.capability());
            LOG_INF("X4", "input.touch ready=%d", nativeTouchAttachBootstrap(touch_api) ? 1 : 0);
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
    // BoardX4Pro's power hooks do not touch T5S3 peripherals. Initialize
    // the shared power mutex before any native app takes its UI/power lock.
    powerManager.begin();
    SETTINGS.loadFromFile();
    I18N.setLanguage(static_cast<Language>(SETTINGS.language));
    UITheme::getInstance().reload();
    if (!setupDisplayAndFonts()) return;
    display.setFlipOutput(SETTINGS.flipUi != 0);
    setupReaderState();
    // Reader's four button hints are laid out in portrait coordinates. The
    // physical X4 is portrait when held with its controls upright; landscape
    // left the Classic Home menu with a negative usable height.
    renderer.setOrientation(GfxRenderer::Portrait);
    if (renderer.getScreenWidth() != 480 || renderer.getScreenHeight() != 800) {
        LOG_ERR("X4", "Reader portrait geometry rejected");
        return;
    }
    if (light_api) (void)light_api->set_level(light_api->context, 0, 1);
    // E-paper retains its previous frame across reset. Present a brief
    // startup frame so a fresh boot is visible before an identical Home
    // image is drawn. This is X4-only and adds no NVS or storage write.
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 72, "Starting Reader");
    renderer.drawCenteredText(UI_10_FONT_ID, 116, "X4 Pro");
    renderer.displayBuffer(DisplayPresentMode::Clean);
    LOG_INF("X4", "boot splash present=%d", provider_surface->lastPresentSucceeded() ? 1 : 0);
    provider_surface->clearPresentStatus();
    const auto &home = UITheme::getInstance().getMetrics();
    const int menu_height = renderer.getScreenHeight() -
        (home.homeTopPadding + home.homeCoverTileHeight + home.homeMenuTopOffset +
         home.buttonHintsHeight);
    const int required_menu_height = 3 * home.menuRowHeight + 2 * home.menuSpacing;
    if (menu_height < required_menu_height) {
        LOG_ERR("X4", "Home menu geometry rejected height=%d required=%d",
                menu_height, required_menu_height);
        return;
    }
    LOG_INF("X4", "home geometry width=%d height=%d menu_height=%d",
            renderer.getScreenWidth(), renderer.getScreenHeight(), menu_height);
    ButtonNavigator::setMappedInputManager(mappedInputManager);
    activityManager.goHome();
    // Home queues its first render before the owner loop starts.
    activityManager.requestUpdate(true);
    showing_home = true;
    LOG_INF("X4", "home activity scheduled=1");

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
    if (!ready) {
        delay(200);
        return;
    }
    mappedInputManager.update();
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
        activityManager.loop();
    }
    delay(20);
}

#endif  // BOARD_XTEINK_X4_PRO
