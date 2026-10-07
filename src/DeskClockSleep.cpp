#include "DeskClockSleep.h"
#include "platform/X4DiagnosticBoot.h"

#include <Board.h>
#include <EpdFont.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_system.h>
#if defined(BOARD_XTEINK_X4_PRO)
#include <driver/rtc_io.h>
#include <sdkconfig.h>
#if !defined(CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER) || !CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER
#error "X4 retained desk clock requires the SDK RTC time source"
#endif
#endif
#include <soc/soc_caps.h>
#include <sys/time.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "fontIds.h"
#include "util/DeskClockTime.h"
#include "util/DeskClockFaces.h"

// These objects exist before setup(), so a timer wake can paint without
// mounting the SD card or starting the activity manager/render task.
extern GfxRenderer renderer;
extern EpdFontFamily ui12FontFamily;
extern EpdFontFamily smallFontFamily;

namespace {
constexpr uint32_t kClockMagic = 0x434C4B33;  // CLK3; discard stale retained clock state.
constexpr uint32_t kClockUiWakeMagic = 0x57414B45;  // WAKE; survive the explicit restart below.
constexpr uint16_t kFullRefreshMinutes = 30;

struct ClockRetention {
  uint32_t magic;
  uint32_t rtcReferenceEpoch;
  int64_t displayedMinuteEpoch;
  uint16_t refreshCount;
  uint8_t timeFormat;
  uint8_t face;
  uint8_t language;
  uint8_t flipUi;
  uint8_t rtcStoresUtc;
  uint8_t rtcVariantHint;
  char timeZoneId[40];
};
RTC_DATA_ATTR ClockRetention clockState = {};
// Unlike RTC_DATA_ATTR, NOINIT is not reloaded from the image on software
// restart. Consume only on that reset cause and clear on every boot.
RTC_NOINIT_ATTR uint32_t clockUiWakeMagic;
bool userWakePending = false;

void renderClockFrame(GfxRenderer& gfx, time_t now) {
  tm local = {};
  const bool valid = halClock.isSystemTimeValid() && localtime_r(&now, &local) != nullptr;
  const bool use12Hour = clockState.timeFormat == CrossPointSettings::TIME_12H;
  const int height = gfx.getScreenHeight();
  gfx.clearScreen();
  DeskClockFaces::draw(gfx, clockState.face, local.tm_hour, local.tm_min, use12Hour, valid);

  char date[32] = {};
  if (valid) strftime(date, sizeof(date), "%Y-%m-%d", &local);
  gfx.drawCenteredText(UI_12_FONT_ID, 32, valid ? date : tr(STR_CLOCK_SET_TIME));
  if (valid && use12Hour) {
    gfx.drawCenteredText(UI_12_FONT_ID, height - 78, ClockFormat::period(local.tm_hour));
  }
  gfx.drawCenteredText(SMALL_FONT_ID, height - 44, tr(STR_CLOCK_WAKE_BUTTON));
}

uint8_t* allocatePreviousFrameBuffer() {
  const size_t bytes = display.getBufferSize();
  auto* buffer = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buffer) {
    buffer = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  }
  return buffer;
}

void drawClock(GfxRenderer& gfx, time_t now, bool fullRefresh, time_t previousDisplayedMinute) {
  if (fullRefresh || previousDisplayedMinute <= 0) {
    renderClockFrame(gfx, now);
    display.setIdlePowerSaving(false);
    gfx.displayBuffer(HalDisplay::FULL_REFRESH);
    display.setIdlePowerSaving(true);
    return;
  }

  uint8_t* previousFrame = allocatePreviousFrameBuffer();
  if (!previousFrame) {
    LOG_ERR("CLOCK", "Could not allocate previous clock frame; using full refresh");
    renderClockFrame(gfx, now);
    display.setIdlePowerSaving(false);
    gfx.displayBuffer(HalDisplay::FULL_REFRESH);
    display.setIdlePowerSaving(true);
    return;
  }

  // Deep sleep discards RAM but leaves the e-paper image in place. Re-render
  // exactly the retained minute that is physically on the panel, copy that
  // 1-bit frame to temporary PSRAM, then render the new minute. HalDisplay uses
  // the two logical frames to clip the panel update to only the changed area.
  renderClockFrame(gfx, previousDisplayedMinute);
  memcpy(previousFrame, display.getFrameBuffer(), display.getBufferSize());
  renderClockFrame(gfx, now);

  display.setIdlePowerSaving(false);
  display.displayBufferDiff(previousFrame, HalDisplay::HALF_REFRESH);
  display.setIdlePowerSaving(true);
  heap_caps_free(previousFrame);
}

// The timer and button are armed together AFTER clearing wake sources. In
// particular, HalGPIO::startDeepSleep() cannot be called here: it clears the
// timer wake source when it configures its normal power/touch wake sources.
// Neither the GT911 interrupt nor touch polling is enabled during clock sleep.
void sleepUntilNextMinute() {
  if (!display.deepSleep()) {
    clockState.magic = 0;
    LOG_ERR("CLOCK", "Display/storage power-down barrier refused clock sleep");
    return;
  }
  // Power/SD teardown has elapsed; calculate the same minute deadline after
  // that bounded work so its duration cannot make every minute wake late.
  timeval now = {};
  gettimeofday(&now, nullptr);
  const uint64_t waitUs = DeskClockTime::untilNextMinuteUs(now.tv_sec, now.tv_usec);
  pinMode(BoardPins::PowerButton, INPUT_PULLUP);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const esp_err_t timerResult = esp_sleep_enable_timer_wakeup(waitUs);
  esp_err_t buttonResult = ESP_FAIL;
#if defined(BOARD_XTEINK_X4_PRO)
  // IDF4.4.7 ext1_wakeup_prepare disables RTC pulls when RTC_PERIPH is off.
  // Keep that tiny domain powered and explicitly configure GPIO3's RTC pull
  // instead of relying on an undocumented external resistor. CPU/radios/SD/
  // touch/panel still enter their existing deep-sleep/off state.
  const auto wakePin = static_cast<gpio_num_t>(BoardPins::PowerButton);
  if (esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON) != ESP_OK ||
      rtc_gpio_init(wakePin) != ESP_OK || rtc_gpio_set_direction(wakePin, RTC_GPIO_MODE_INPUT_ONLY) != ESP_OK ||
      rtc_gpio_pullup_en(wakePin) != ESP_OK || rtc_gpio_pulldown_dis(wakePin) != ESP_OK) {
    clockState.magic = 0;
    LOG_ERR("CLOCK", "RTC wake-pad setup failed; restarting");
    ESP.restart();
    return;
  }
#endif
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  buttonResult = esp_deep_sleep_enable_gpio_wakeup(1ULL << BoardPins::PowerButton, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  buttonResult = esp_sleep_enable_ext1_wakeup(1ULL << BoardPins::PowerButton, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  if (timerResult != ESP_OK || buttonResult != ESP_OK) {
    clockState.magic = 0;
    LOG_ERR("CLOCK", "Deep-sleep wake setup failed: timer=%s button=%s", esp_err_to_name(timerResult),
            esp_err_to_name(buttonResult));
    ESP.restart();  // Never strand the user behind an unarmed button wake.
    return;
  }
  LOG_INF("CLOCK", "Deep sleep armed: timer-us=%llu power-pin=%u time-valid=%d",
          static_cast<unsigned long long>(waitUs), static_cast<unsigned>(BoardPins::PowerButton),
          halClock.isSystemTimeValid());
  esp_deep_sleep_start();
}

void paintAndSleep(GfxRenderer& gfx, bool timerWake) {
  gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  gfx.setRenderMode(GfxRenderer::BW);
  // A deep-sleep boot loses the display controller's previous framebuffer, but
  // the physical e-paper image remains. Match the normal seamless boot policy.
  const bool fullRefresh = !timerWake || (clockState.refreshCount % kFullRefreshMinutes == 0);
  if (timerWake && !fullRefresh) display.suppressInitialFullRefresh();

  timeval before = {};
  timeval after = {};
  bool currentMinute = false;
  for (unsigned catchup = 0; catchup < 3u; ++catchup) {
    gettimeofday(&before, nullptr);
    const time_t previousDisplayedMinute = static_cast<time_t>(clockState.displayedMinuteEpoch);
    drawClock(gfx, before.tv_sec, fullRefresh, previousDisplayedMinute);
#if defined(BOARD_XTEINK_X4_PRO)
    if (!display.lastPresentSucceeded()) {
      clockState.magic = 0;
      LOG_ERR("CLOCK", "Clock presentation failed; retained minute unchanged");
      return;
    }
#endif
    clockState.displayedMinuteEpoch = (before.tv_sec / 60) * 60;
    gettimeofday(&after, nullptr);
    // An unusually slow refresh can cross a minute boundary. Never sleep for
    // another minute showing the previous one.
    if (before.tv_sec / 60 == after.tv_sec / 60) { currentMinute = true; break; }
  }
  if (!currentMinute) {
    clockState.magic = 0;
    LOG_ERR("CLOCK", "Clock refresh repeatedly crossed minute boundary");
    return;
  }

  // If the button was pressed while painting, leave clock mode and let normal
  // startup handle that press, instead of sleeping through it.
  if (digitalRead(BoardPins::PowerButton) == LOW) {
    // A press can arrive after a timer wake while the clock is repainting.
    // Preserve that user intent across the explicit software restart so normal
    // startup can skip the cold-boot splash and continue directly to resume/Home.
    clockUiWakeMagic = kClockUiWakeMagic;
    clockState.magic = 0;
    ESP.restart();
    return;
  }
  ++clockState.refreshCount;
  sleepUntilNextMinute();
}
}  // namespace

void DeskClockSleep::run(GfxRenderer& gfx, HalGPIO& input) {
  RenderLock lock;
  const auto previousOrientation = gfx.getOrientation();
  const auto previousRenderMode = gfx.getRenderMode();
  struct RestoreRenderer {
    GfxRenderer& gfx;
    GfxRenderer::Orientation orientation;
    GfxRenderer::RenderMode mode;
    ~RestoreRenderer() { gfx.setOrientation(orientation); gfx.setRenderMode(mode); }
  } restore{gfx, previousOrientation, previousRenderMode};
  input.update();
  // Don't immediately wake from the press that entered clock mode.
  const uint32_t releaseBegan = millis();
  while (digitalRead(BoardPins::PowerButton) == LOW) {
    if (static_cast<uint32_t>(millis() - releaseBegan) >= 2000u) {
      LOG_ERR("CLOCK", "Power button remained held; clock entry cancelled");
      return;
    }
    delay(10);
    input.update();
  }

  clockUiWakeMagic = 0;
  userWakePending = false;
  clockState = {};
  clockState.magic = kClockMagic;
  clockState.rtcReferenceEpoch = SETTINGS.rtcReferenceEpoch;
  clockState.timeFormat = SETTINGS.timeFormat;
  clockState.face = DeskClockFaces::sanitize(SETTINGS.clockFace);
  clockState.language = SETTINGS.language;
  clockState.flipUi = SETTINGS.flipUi;
  clockState.rtcStoresUtc = SETTINGS.rtcStoresUtc;
  clockState.rtcVariantHint = SETTINGS.rtcVariantHint;
  memcpy(clockState.timeZoneId, SETTINGS.timeZoneId, sizeof(clockState.timeZoneId));
  clockState.timeZoneId[sizeof(clockState.timeZoneId) - 1] = '\0';
  LOG_INF("CLOCK", "Clock entry: time-valid=%d face=%u", halClock.isSystemTimeValid(), clockState.face);

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Board::setBacklightLevel(0);
#if !defined(BOARD_XTEINK_X4_PRO)
  halTiltSensor.deepSleep();
#endif
  paintAndSleep(gfx, false);
}

bool DeskClockSleep::resumeAfterTimerWake() {
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  LOG_INF("CLOCK", "Wake cause=%d retained=%d time-valid=%d", static_cast<int>(wakeCause),
          clockState.magic == kClockMagic, halClock.isSystemTimeValid());
#if defined(BOARD_XTEINK_X4_PRO)
  // EXT1 used the RTC pad. Return it to digital GPIO before the existing
  // ordinary button provider is allowed to acquire and poll it again.
  const auto wakePin = static_cast<gpio_num_t>(BoardPins::PowerButton);
  if (wakeCause != ESP_SLEEP_WAKEUP_UNDEFINED &&
      (rtc_gpio_hold_dis(wakePin) != ESP_OK || rtc_gpio_deinit(wakePin) != ESP_OK)) {
    clockState.magic = 0;
    return false;
  }
  pinMode(BoardPins::PowerButton, INPUT_PULLUP);
#endif

  // If a button arrived while a timer-wake repaint was already running, the
  // clock path performs an explicit restart. RTC retention is the only signal
  // available on the following software-reset boot.
  const bool requestedUiWake = esp_reset_reason() == ESP_RST_SW && clockUiWakeMagic == kClockUiWakeMagic;
  clockUiWakeMagic = 0;
  if (requestedUiWake) {
    clockState.magic = 0;
    userWakePending = true;
    return false;
  }

  const bool retainedClock = clockState.magic == kClockMagic;
  const bool timerWake = wakeCause == ESP_SLEEP_WAKEUP_TIMER;
  if (!timerWake || !retainedClock) {
    // sleepUntilNextMinute() disables every wake source before arming only the
    // minute timer and power button. Therefore any defined non-timer wake while
    // retained clock state is valid is the user's request to leave desk-clock
    // mode. Panic/reset/cold boots report UNDEFINED and retain normal boot UI.
    userWakePending =
        retainedClock && wakeCause != ESP_SLEEP_WAKEUP_UNDEFINED;
    clockState.magic = 0;
    return false;
  }

  // Fast resume skips normal filesystem/UI/input/network startup. External
  // display composition may read and release the isolated read-only SD boot
  // store to obtain its ordinary provider; no storage.volume mount occurs.
#if defined(BOARD_XTEINK_X4_PRO)
  // Reject an unset/lost SDK epoch before loading any display dependencies.
  // Ordinary startup owns RTC acquisition and time-setting UI recovery.
  if (!halClock.isSystemTimeValid()) {
    clockState.magic = 0;
    LOG_ERR("CLOCK", "Clock timer wake has no valid time; returning to normal boot");
    return false;
  }
  if (!x4BeginClockDisplay()) {
    clockState.magic = 0;
    return false;
  }
#else
  Board::begin();
  halClock.begin();
#endif
  halClock.configure(clockState.timeZoneId, clockState.rtcStoresUtc != 0,
                     clockState.rtcVariantHint, clockState.rtcReferenceEpoch);
#if defined(BOARD_XTEINK_X4_PRO)
  const bool clockAvailable = halClock.isSystemTimeValid();
#else
  const bool clockAvailable = halClock.syncSystemTimeFromRtc() || halClock.isSystemTimeValid();
#endif
  if (!clockAvailable) {
    LOG_ERR("CLOCK", "Clock timer wake has no valid time; returning to normal boot");
    clockState.magic = 0;
    return false;
  }
  I18N.setLanguage(static_cast<Language>(clockState.language));
  // The physical e-paper image survives deep sleep. Do not use the normal
  // M5GFX init path here: gfx->init() explicitly clears EPD panels.
  display.begin(false);
  if (display.getFrameBuffer() == nullptr) {
    LOG_ERR("CLOCK", "Clock display initialization failed; returning to normal boot");
    clockState.magic = 0;
    return false;
  }
  renderer.begin();
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  display.setFlipOutput(clockState.flipUi != 0);
  paintAndSleep(renderer, true);
  return false;  // Deep sleep never returns; the fallback is a normal boot.
}

bool DeskClockSleep::consumeUserWake() {
  const bool pending = userWakePending;
  userWakePending = false;
  return pending;
}
