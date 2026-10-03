#if defined(RISCRTE_PROFILE_HEADLESS)
#include "runtime/boot/HeadlessRuntime.h"
void setup() { RuntimeBoot::setup(); }
void loop() { RuntimeBoot::loop(); }
#else
#include <Arduino.h>
#include <Board.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#include <cstring>
#include <esp_heap_caps.h>
#include <esp32-hal-psram.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DeskClockSleep.h"
#include "native/NativeAppHost.h"
#include "native/NativeReaderEntry.h"
#include "native/InstalledAppPath.h"
#include "native/NativeStreamBridge.h"
#include "runtime/boot/DefaultAppSelection.h"
#include "native/NativeNavigationInput.h"
#include "native/NativeTouchInput.h"
#include "runtime/network/PsramTlsAllocator.h"
#include "runtime/packages/InstalledCapabilityResolver.h"
#include "KOReaderCredentialStore.h"
#include "PowerControl.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "components/StartupScreen.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// Fonts
EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont notoserif12RegularFont(&notoserif_12_regular);
EpdFont notoserif12BoldFont(&notoserif_12_bold);
EpdFont notoserif12ItalicFont(&notoserif_12_italic);
EpdFont notoserif12BoldItalicFont(&notoserif_12_bolditalic);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);
EpdFont notoserif16RegularFont(&notoserif_16_regular);
EpdFont notoserif16BoldFont(&notoserif_16_bold);
EpdFont notoserif16ItalicFont(&notoserif_16_italic);
EpdFont notoserif16BoldItalicFont(&notoserif_16_bolditalic);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);
EpdFont notoserif18RegularFont(&notoserif_18_regular);
EpdFont notoserif18BoldFont(&notoserif_18_bold);
EpdFont notoserif18ItalicFont(&notoserif_18_italic);
EpdFont notoserif18BoldItalicFont(&notoserif_18_bolditalic);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

EpdFont opendyslexic8RegularFont(&opendyslexic_8_regular);
EpdFont opendyslexic8BoldFont(&opendyslexic_8_bold);
EpdFont opendyslexic8ItalicFont(&opendyslexic_8_italic);
EpdFont opendyslexic8BoldItalicFont(&opendyslexic_8_bolditalic);
EpdFontFamily opendyslexic8FontFamily(&opendyslexic8RegularFont, &opendyslexic8BoldFont, &opendyslexic8ItalicFont,
                                      &opendyslexic8BoldItalicFont);
EpdFont opendyslexic10RegularFont(&opendyslexic_10_regular);
EpdFont opendyslexic10BoldFont(&opendyslexic_10_bold);
EpdFont opendyslexic10ItalicFont(&opendyslexic_10_italic);
EpdFont opendyslexic10BoldItalicFont(&opendyslexic_10_bolditalic);
EpdFontFamily opendyslexic10FontFamily(&opendyslexic10RegularFont, &opendyslexic10BoldFont, &opendyslexic10ItalicFont,
                                       &opendyslexic10BoldItalicFont);
EpdFont opendyslexic12RegularFont(&opendyslexic_12_regular);
EpdFont opendyslexic12BoldFont(&opendyslexic_12_bold);
EpdFont opendyslexic12ItalicFont(&opendyslexic_12_italic);
EpdFont opendyslexic12BoldItalicFont(&opendyslexic_12_bolditalic);
EpdFontFamily opendyslexic12FontFamily(&opendyslexic12RegularFont, &opendyslexic12BoldFont, &opendyslexic12ItalicFont,
                                       &opendyslexic12BoldItalicFont);
EpdFont opendyslexic14RegularFont(&opendyslexic_14_regular);
EpdFont opendyslexic14BoldFont(&opendyslexic_14_bold);
EpdFont opendyslexic14ItalicFont(&opendyslexic_14_italic);
EpdFont opendyslexic14BoldItalicFont(&opendyslexic_14_bolditalic);
EpdFontFamily opendyslexic14FontFamily(&opendyslexic14RegularFont, &opendyslexic14BoldFont, &opendyslexic14ItalicFont,
                                       &opendyslexic14BoldItalicFont);
#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;
constexpr unsigned long kBootConfirmHoldMs = 700;
constexpr unsigned long kPcaButtonPowerOffHoldMs = 2000;
constexpr unsigned long kEpd47PowerOffHoldMs = 2000;

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    abort = gpio.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    powerManager.startDeepSleep(gpio);
  }
}
void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

bool shouldSuppressDeepSleepForDebug() {
#ifdef ENABLE_SERIAL_LOG
  return Serial;
#else
  return false;
#endif
}

bool suspendInputProvidersForSleep() {
  // Touch and navigation share lower provider dependencies. Release the touch
  // lease first so navigation can prove the entire graph quiescent before the
  // board tears down SD/I2C/power for sleep.
  if (!nativeTouchSuspend()) {
    LOG_ERR("INPUT", "Sleep refused: touch provider has not quiesced");
    (void)nativeTouchResume();
    return false;
  }
  if (!nativeNavigationSuspend()) {
    LOG_ERR("INPUT", "Sleep refused: navigation/provider graph has not quiesced");
    nativeNavigationResume();
    (void)nativeTouchResume();
    return false;
  }
  return true;
}

void resumeInputProvidersAfterSleep() {
  // Bootstrap/navigation first; touch can then join the already healthy graph.
  nativeNavigationResume();
  (void)nativeTouchResume();
}

// Enter deep sleep mode
void enterDeepSleep() {
  if (NativeReaderEntry::deferSleep(NativeReaderEntry::Action::Sleep)) return;
  const bool deskClock = SETTINGS.sleepScreen == CrossPointSettings::DIGITAL_CLOCK;
  if (!deskClock && shouldSuppressDeepSleepForDebug()) {
    LOG_DBG("MAIN", "Deep sleep suppressed while serial is connected");
    waitForPowerRelease();
    return;
  }

  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  if (!suspendInputProvidersForSleep()) return;
  APP_STATE.lastSleepFromReader = APP_STATE.lastSleepFromReader || activityManager.isReaderActivityInStack();
  APP_STATE.saveToFile();

  activityManager.goToSleep();
  Board::setBacklightLevel(0);

  if (deskClock) {
    // SleepActivity has closed the reader and saved its position. The input
    // provider graph stays quiesced while the retained clock owns sleep/wake.
    DeskClockSleep::run(renderer, gpio);
    resumeInputProvidersAfterSleep();
    Board::setBacklightLevel(SETTINGS.backlightLevel);
    renderer.requestNextRefresh(HalDisplay::FULL_REFRESH);
    if (SETTINGS.resumeReaderOnBoot && APP_STATE.lastSleepFromReader && !APP_STATE.openEpubPath.empty()) {
      activityManager.goToReader(APP_STATE.openEpubPath, HalDisplay::FULL_REFRESH);
    } else {
      activityManager.goHome();
    }
    return;
  }

  halTiltSensor.deepSleep();
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void enterDeepSleepKeepingScreen(bool wakeOnTouch = true) {
  if (NativeReaderEntry::deferSleep(NativeReaderEntry::Action::SleepKeepingScreen, wakeOnTouch)) return;
  HalPowerManager::Lock powerLock;
  if (!suspendInputProvidersForSleep()) return;
  APP_STATE.lastSleepFromReader = APP_STATE.lastSleepFromReader || activityManager.isReaderActivityInStack();
  APP_STATE.saveToFile();

  Board::setBacklightLevel(0);
  halTiltSensor.deepSleep();
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep with current screen preserved, wakeOnTouch=%d", wakeOnTouch ? 1 : 0);

  powerManager.startDeepSleep(gpio, wakeOnTouch);
}

void enterPowerOffKeepingScreen(const char* status) {
  if (NativeReaderEntry::deferSleep(NativeReaderEntry::Action::PowerOff)) return;
  (void)status;  // Status line intentionally not shown; the sleep screen setting is used instead.
  if (!suspendInputProvidersForSleep()) return;
  {
    HalPowerManager::Lock powerLock;
    APP_STATE.lastSleepFromReader = activityManager.isReaderActivityInStack();
    APP_STATE.saveToFile();

    // Render the sleep screen before putting the display to sleep: deepSleep() also
    // disconnects the SD card (Board::deinitForSleep), which the sleep screen may need
    // to read (e.g. custom/cover images).
    activityManager.goToSleep(/*poweringOff=*/true);
    Board::setBacklightLevel(0);
    display.deepSleep();
    if (Board::capabilities().hasHardPowerOff) {
      if (Board::shutdownBatteryPower()) {
        delay(1500);
        LOG_DBG("MAIN", "Battery power shutdown returned; falling back to deep sleep");
      } else {
        LOG_ERR("MAIN", "Battery power shutdown failed or was rejected; falling back to deep sleep");
      }
    } else {
      LOG_DBG("MAIN", "Hard power-off is unavailable; using deep sleep as power-off");
    }
  }

  enterDeepSleepKeepingScreen(false);
}

// Set by activities (e.g. the reader menu's Shut Down button) to request a full
// power-off. Consumed at the top of loop() so the battery-cut runs in the main-loop
// context rather than inside an activity's call stack.
bool g_shutdownRequested = false;
bool g_displayBootFailed = false;
static bool g_readerStartPending = false;
static bool g_readerResumeOnBoot = false;
static bool g_readerDeskClockWake = false;
static bool g_readerEntryEligible = false;
void requestShutdown() { g_shutdownRequested = true; }

bool setupDisplayAndFonts() {
  // This runs before any display backend initialization. A bad width, stride,
  // format or safe-area edit must be rejected before it can reconfigure the
  // panel or disturb the retained e-paper image.
  if (!renderer.preflightSurface()) {
    LOG_ERR("MAIN", "Display metadata preflight failed; panel backend was not touched");
    return false;
  }

  // Preserve the physical image while the already-validated backend starts.
  display.begin(false);
  if (!display.isReady()) {
    LOG_ERR("MAIN", "Display backend initialization failed; retained panel image left untouched");
    return false;
  }
  if (!renderer.begin()) {
    LOG_ERR("MAIN", "Renderer initialization failed; showing emergency display code 0xD1");
    (void)display.showEmergencyFailurePattern(0xD1);
    return false;
  }
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized and renderer surface validated");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);

  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
  renderer.insertFont(OPENDYSLEXIC_8_FONT_ID, opendyslexic8FontFamily);
  renderer.insertFont(OPENDYSLEXIC_10_FONT_ID, opendyslexic10FontFamily);
  renderer.insertFont(OPENDYSLEXIC_12_FONT_ID, opendyslexic12FontFamily);
  renderer.insertFont(OPENDYSLEXIC_14_FONT_ID, opendyslexic14FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  LOG_DBG("MAIN", "Built-in fonts setup");
  return true;
}

void ensureSdFontLoaded() { sdFontSystem.ensureLoaded(renderer); }

HalDisplay::RefreshMode readerResumeRefreshMode() {
  switch (SETTINGS.readerDisplayMode) {
    case CrossPointSettings::READER_DISPLAY_FAST:
      return HalDisplay::FAST_REFRESH;
    case CrossPointSettings::READER_DISPLAY_STANDARD:
      return HalDisplay::BALANCED_REFRESH;
    case CrossPointSettings::READER_DISPLAY_QUALITY:
    default:
      return HalDisplay::HALF_REFRESH;
  }
}

bool shouldResumeReaderOnBoot() {
  return SETTINGS.resumeReaderOnBoot && !APP_STATE.openEpubPath.empty() && APP_STATE.lastSleepFromReader &&
         !mappedInputManager.isPressed(MappedInputManager::Button::Back) && APP_STATE.readerActivityLoadCount == 0;
}

void logPlatformInputHealth() {
  auto* snapshot = RuntimePackages::captureInstalledCapabilities();
  if (!snapshot) {
    LOG_ERR("INPUT", "Platform driver inventory unavailable; touch/controller capabilities cannot be verified");
    return;
  }
  const uint32_t i2c = RuntimePackages::versionInInstalledSnapshot(snapshot, "i2c.bus");
  const uint32_t clock = RuntimePackages::versionInInstalledSnapshot(snapshot, "platform.clock");
  const uint32_t touch = RuntimePackages::versionInInstalledSnapshot(snapshot, "input.touch.raw");
  const uint32_t navigation = RuntimePackages::versionInInstalledSnapshot(snapshot, "input.navigation");
  RuntimePackages::releaseInstalledCapabilities(snapshot);
  LOG_INF("INPUT", "Platform driver health: i2c.bus=%lu platform.clock=%lu touch=%lu navigation=%lu",
          static_cast<unsigned long>(i2c), static_cast<unsigned long>(clock),
          static_cast<unsigned long>(touch), static_cast<unsigned long>(navigation));
  if (!i2c || !clock) {
    LOG_ERR("INPUT",
            "Foundational platform drivers incomplete: install i2c-esp32s3-v2 and platform-clock-v1; "
            "touch and USB/controller providers may be unavailable");
  } else {
    if (!touch) LOG_ERR("INPUT", "Touch capability unavailable: install/repair gt911-touch");
    if (SETTINGS.externalInputNavigation && !navigation)
      LOG_ERR("INPUT", "Controller navigation capability unavailable: install/repair usb-ui-navigation dependency stack");
  }
}

static void startReaderApplication() {
  if (!g_readerStartPending) return;
  g_readerStartPending = false;
  const auto prepareStartupRefresh = [](HalDisplay::RefreshMode refreshMode) {
    display.suppressInitialFullRefresh();
    renderer.requestNextRefresh(refreshMode);
  };
  if (!g_readerResumeOnBoot) {
    prepareStartupRefresh(HalDisplay::HALF_REFRESH);
    if (!g_readerDeskClockWake) {
      RenderLock lock;
      StartupScreen::armBootFade();
    }
    // A cold boot fades the logo when Home is ready. A desk-clock user wake
    // has no splash to fade and proceeds directly into Home.
    activityManager.goHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    display.suppressInitialFullRefresh();
    activityManager.goToReader(path, readerResumeRefreshMode());
  }
}

void setup() {
  t1 = millis();

  // Keep large general-purpose allocations out of scarce internal RAM without
  // relocating small networking/stream/storage control objects. App Store bulk
  // metadata has explicit PSRAM allocators; the global threshold remains high
  // enough to avoid changing transport object placement.
  if (psramFound()) {
    heap_caps_malloc_extmem_enable(1024);
  }
  RuntimeNetwork::enablePsramTlsAllocations();

  HalSystem::begin();
  // Timer wakes never reach this point. A true value means the user explicitly
  // left retained desk-clock deep sleep and normal startup should resume
  // content/Home without replaying the cold-boot splash.
  const bool deskClockUserWake = DeskClockSleep::consumeUserWake();
  gpio.begin();
  powerManager.begin();
  halClock.begin();
  halTiltSensor.begin();

#ifdef ENABLE_SERIAL_LOG
  Serial.begin(115200);
  const unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart) < 500) {
    delay(10);
  }
#endif

  LOG_INF("MAIN", "Hardware detect: %s", gpio.getDeviceName());

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    if (!setupDisplayAndFonts()) {
      g_displayBootFailed = true;
      return;
    }
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  HalSystem::checkPanic();

  SETTINGS.loadFromFile();
  LOG_DBG("MAIN", "Clock settings: tz=%s rtcStoresUtc=%u rtcVariantHint=%u rtcReferenceEpoch=%lu",
          SETTINGS.timeZoneId, static_cast<unsigned>(SETTINGS.rtcStoresUtc),
          static_cast<unsigned>(SETTINGS.rtcVariantHint), static_cast<unsigned long>(SETTINGS.rtcReferenceEpoch));
  halClock.configure(SETTINGS.timeZoneId, SETTINGS.rtcStoresUtc != 0, SETTINGS.rtcVariantHint, SETTINGS.rtcReferenceEpoch);
  if (!halClock.syncSystemTimeFromRtc()) {
    LOG_DBG("MAIN", "RTC time unavailable or invalid at boot");
  } else if (SETTINGS.rtcStoresUtc != static_cast<uint8_t>(halClock.getRtcStoresUtc())) {
    SETTINGS.rtcStoresUtc = halClock.getRtcStoresUtc() ? 1 : 0;
    if (SETTINGS.saveToFile()) {
      LOG_DBG("MAIN", "Updated RTC storage mode setting after boot auto-correction");
    } else {
      LOG_ERR("MAIN", "Failed to persist RTC storage mode auto-correction");
    }
  }
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  const auto wakeupReason = gpio.getWakeupReason();
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button wakeup");
      gpio.verifyPowerButtonWakeup(10, true);
      break;
    case HalGPIO::WakeupReason::Touch:
      LOG_DBG("MAIN", "Wakeup reason: Touch");
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  Board::setBacklightLevel(SETTINGS.backlightLevel);

  // Recovery firmware mode: hold left side button (BTN_UP) together with the power button at
  // boot to skip directly to the SD-card firmware update screen. Useful on devices where USB
  // flashing has been locked down (e.g. recent X3 firmware).
  bool recoveryFirmwareMode = false;
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // Refresh the cached button state a few times — isPressed() needs ~half a second to settle
    // after boot per the HalGPIO contract. Use a millis-based deadline so we always wait the full
    // settle window even if the loop body takes longer than expected on slow boots.
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  const bool animateBoot = !recoveryFirmwareMode && !HalSystem::isRebootFromPanic() && !deskClockUserWake;
  if (!setupDisplayAndFonts()) {
    g_displayBootFailed = true;
    return;
  }
  // Touch is an optional installed provider capability. Do not attempt to
  // activate it during setup: input.navigation gets the first provider-graph
  // opportunity from MappedInputManager::update(), so a missing touch package
  // can never strand USB/controller navigation before Driver Manager is usable.
  // nativeTouchTick() activates touch later in the normal input loop.
  LOG_INF("MAIN", "Touch provider activation deferred to input loop");
  display.setFlipOutput(SETTINGS.flipUi != 0);

  // Start the independent animation before SD font discovery, state loading,
  // provider admission and Home/reader preparation. Recovery bypasses it.
  if (animateBoot) {
    RenderLock lock;
    StartupScreen::boot(renderer);
  }

  sdFontSystem.begin(renderer);
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  // Keep provider loading on the normal owner task. The video worker only
  // submits pixels; it cannot contend with SD/module/renderer initialization.
  if (!recoveryFirmwareMode && !HalSystem::isRebootFromPanic()) {
    logPlatformInputHealth();
    mappedInputManager.update();  // Navigation first, then optional touch.
    LOG_INF("MAIN", "Startup services ready in %lu ms", static_cast<unsigned long>(millis() - t1));
  }
  const bool resumeReaderOnBoot = shouldResumeReaderOnBoot();
  const auto prepareStartupRefresh = [](HalDisplay::RefreshMode refreshMode) {
    display.suppressInitialFullRefresh();
    renderer.requestNextRefresh(refreshMode);
  };

  if (recoveryFirmwareMode) {
    prepareStartupRefresh(HalDisplay::HALF_REFRESH);
    // Skip the boot splash and jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true),
        HalDisplay::HALF_REFRESH);
  } else if (HalSystem::isRebootFromPanic()) {
    prepareStartupRefresh(HalDisplay::HALF_REFRESH);
    // If we rebooted from a panic, go directly to the crash report screen to show the panic info.
    activityManager.goToCrashReport();
  } else {
    // The ELF's first pump enters exactly the previous Home/book destination.
    // The firmware remains the owner of its ActivityManager and saved state.
    g_readerResumeOnBoot = resumeReaderOnBoot;
    g_readerDeskClockWake = deskClockUserWake;
    g_readerStartPending = true;
    g_readerEntryEligible = true;
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

static void readerApplicationLoop() {
  if (g_displayBootFailed) {
    // Do not touch ActivityManager/renderer after failed display bootstrap.
    // Leave the retained image or emergency failure pattern stable for diagnosis.
    delay(250);
    return;
  }

  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  mappedInputManager.update();
  // Generic installed-provider progress is independent of GUI/device polling.
  // Foreground synchronous apps use the same owner-task hook in stream calls.
  nativeProviderOwnerTick();
  // External power/connection changes allow a new bounded admission attempt.
  // No provider inventory scans on every frame after a failed/missing provider.
  if (gpio.wasUsbStateChanged()) nativeNavigationRetry();
  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());

  // Handle a shutdown requested by an activity (e.g. the reader menu Shut Down button).
  if (g_shutdownRequested) {
    g_shutdownRequested = false;
    enterPowerOffKeepingScreen("");
    return;
  }

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (NativeReaderEntry::consumeResume()) lastActivityTime = millis();
  if (mappedInputManager.wasAnyPressed() || mappedInputManager.wasAnyReleased() ||
      nativeNavigationFrame().buttons || nativeTouchHadActivity() || halTiltSensor.hadActivity() ||
      activityManager.preventAutoSleep()
#ifdef ENABLE_SERIAL_LOG
      || (Serial && SETTINGS.sleepScreen != CrossPointSettings::DIGITAL_CLOCK)
#endif
  ) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  static bool screenshotButtonsReleased = true;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  } else {
    screenshotButtonsReleased = true;
  }

  bool hasHardwareButtonTap = false;
  auto hardwareButtonTap = MappedInputManager::Button::Up;
  auto queueHardwareButtonTap = [&](const MappedInputManager::Button button) {
    if (!hasHardwareButtonTap) {
      hardwareButtonTap = button;
      hasHardwareButtonTap = true;
    }
  };
  const bool isReaderPage = activityManager.isReaderPageActivity();
  // Reader page-turn direction of the two physical buttons follows the Side Button Layout
  // setting. BOOT plays the "up" side-button role and IO48/PCA plays the "down" role, so
  // NEXT_PREV swaps which one turns forward vs back. A 180° UI flip swaps the two physical
  // buttons as well (top is now bottom), composing with the side-layout swap via XOR.
  const bool flipUi = SETTINGS.flipUi != 0;
  const bool swapSideButtons = SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV;
  const bool effectiveSwap = swapSideButtons != flipUi;
  const auto bootPageButton = effectiveSwap ? MappedInputManager::Button::PageForward
                                            : MappedInputManager::Button::PageBack;
  const auto pcaPageButton = effectiveSwap ? MappedInputManager::Button::PageBack
                                           : MappedInputManager::Button::PageForward;
  // Non-reader navigation: BOOT is Up and IO48 is Down, swapped when the UI is flipped.
  const auto bootNavButton = flipUi ? MappedInputManager::Button::Down : MappedInputManager::Button::Up;
  const auto pcaNavButton = flipUi ? MappedInputManager::Button::Up : MappedInputManager::Button::Down;

  static bool powerButtonLongPressHandled = false;
  if (gpio.deviceIsEpd47()) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) {
      if (!powerButtonLongPressHandled && gpio.getHeldTime() >= kEpd47PowerOffHoldMs) {
        LOG_DBG("MAIN", "EPD47 power button long press requested deep-sleep power-off");
        powerButtonLongPressHandled = true;
        enterPowerOffKeepingScreen("Powered off");
        return;
      }
    } else {
      if (gpio.wasReleased(HalGPIO::BTN_POWER) && !powerButtonLongPressHandled) {
        LOG_DBG("MAIN", "EPD47 power button short press mapped to Back");
        queueHardwareButtonTap(MappedInputManager::Button::Back);
      }
      powerButtonLongPressHandled = false;
    }
  } else {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) {
      if (!powerButtonLongPressHandled && gpio.getHeldTime() >= kBootConfirmHoldMs) {
        LOG_DBG("MAIN", "BOOT long press mapped to Confirm");
        powerButtonLongPressHandled = true;
        queueHardwareButtonTap(MappedInputManager::Button::Confirm);
      }
    } else {
      if (gpio.wasReleased(HalGPIO::BTN_POWER) && !powerButtonLongPressHandled) {
        LOG_DBG("MAIN", "BOOT short press mapped to %s",
                isReaderPage ? (effectiveSwap ? "PageForward" : "PageBack") : (flipUi ? "Down" : "Up"));
        queueHardwareButtonTap(isReaderPage ? bootPageButton : bootNavButton);
      }
      powerButtonLongPressHandled = false;
    }
  }

  static bool pcaPowerOffHandled = false;
  if (!gpio.isPressed(HalGPIO::BTN_PCA)) {
    if (gpio.wasReleased(HalGPIO::BTN_PCA) && !pcaPowerOffHandled) {
      LOG_DBG("MAIN", "PCA9535 button short press mapped to %s",
              isReaderPage ? (effectiveSwap ? "PageBack" : "PageForward") : (flipUi ? "Up" : "Down"));
      queueHardwareButtonTap(isReaderPage ? pcaPageButton : pcaNavButton);
    }
    pcaPowerOffHandled = false;
  } else if (!pcaPowerOffHandled && gpio.getHeldTime() >= kPcaButtonPowerOffHoldMs) {
    LOG_DBG("MAIN", "PCA9535 button long press BQ25896 shutdown request");
    pcaPowerOffHandled = true;
    enterPowerOffKeepingScreen("Shutting down...");
    return;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    if (gpio.isUsbConnected() && SETTINGS.sleepScreen != CrossPointSettings::DIGITAL_CLOCK) {
      LOG_DBG("SLP", "Auto sleep skipped after %lu ms of inactivity because USB is connected", sleepTimeoutMs);
      lastActivityTime = millis();
    } else {
      LOG_DBG("SLP", "Auto sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
      enterDeepSleep();
      // Desk-clock light sleep returns on user wake; start a fresh idle period.
      lastActivityTime = millis();
      return;
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  if (hasHardwareButtonTap) {
    mappedInputManager.injectButtonTap(hardwareButtonTap);
  }
  activityManager.loop();
  if (consumeNativeAppReturn()) lastActivityTime = millis();
  if (hasHardwareButtonTap) {
    mappedInputManager.clearInjectedButtonTap();
  }
  if (NativeReaderEntry::pending()) return;
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}

static void runReaderSleep(NativeReaderEntry::Action action, bool wakeOnTouch) {
  switch (action) {
    case NativeReaderEntry::Action::Sleep: enterDeepSleep(); break;
    case NativeReaderEntry::Action::SleepKeepingScreen: enterDeepSleepKeepingScreen(wakeOnTouch); break;
    case NativeReaderEntry::Action::PowerOff: enterPowerOffKeepingScreen(""); break;
    default: break;
  }
}

void loop() {
  if (g_displayBootFailed || NativeReaderEntry::blocked()) { delay(250); return; }
  static bool defaultAppChecked = false;
  if (!defaultAppChecked) {
    defaultAppChecked = true;
    // Recovery and panic handling stay independent of installed applications.
    if (g_readerEntryEligible) {
      char artifact[96]{};
      const auto choice = RuntimeDefaultApp::read(artifact);
      if (choice == RuntimeDefaultApp::Selection::Absent)
        std::strcpy(artifact, "default.elf");
      if (choice != RuntimeDefaultApp::Selection::Invalid) {
        std::string installed;
        if (resolveInstalledAppPath(artifact, installed)) {
          LOG_INF("APP", "default start artifact=%s", artifact);
          if (!std::strcmp(artifact, "default.elf")) {
            const bool okay = NativeReaderEntry::run(installed.c_str(), renderer, mappedInputManager,
                startReaderApplication, readerApplicationLoop, runReaderSleep);
            LOG_INF("APP", "Reader entry returned okay=%d", static_cast<int>(okay));
            if (NativeReaderEntry::blocked()) return;
          } else {
            startReaderApplication();
            const auto result = runNativeApp(installed.c_str(), renderer, mappedInputManager);
            LOG_INF("APP", "default returned result=%d", static_cast<int>(result));
            activityManager.goHome();
          }
        } else if (choice == RuntimeDefaultApp::Selection::Ready) {
          LOG_ERR("APP", "default unavailable artifact=%s", artifact);
        }
      } else {
        LOG_ERR("APP", "default selector invalid; embedded GUI remains available");
      }
    }
  }
  // An absent/bad entry is tried once. Preserve the existing recovery GUI and
  // the original Home/book-resume destination, without an unconditional Home.
  startReaderApplication();
  readerApplicationLoop();
}

#endif // RISCRTE_PROFILE_HEADLESS
