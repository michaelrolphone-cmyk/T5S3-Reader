#include "DeskClockSleep.h"

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
#include <soc/soc_caps.h>
#include <sys/time.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "fontIds.h"
#include "util/DeskClockTime.h"

// These objects exist before setup(), so a timer wake can paint without
// mounting the SD card or starting the activity manager/render task.
extern GfxRenderer renderer;
extern EpdFontFamily ui12FontFamily;
extern EpdFontFamily smallFontFamily;

namespace {
constexpr uint32_t kClockMagic = 0x434C4B32;  // CLK2; discard stale retained clock state.
constexpr uint16_t kFullRefreshMinutes = 30;

struct ClockRetention {
  uint32_t magic;
  uint32_t rtcReferenceEpoch;
  int64_t displayedMinuteEpoch;
  uint16_t refreshCount;
  uint8_t timeFormat;
  uint8_t language;
  uint8_t flipUi;
  uint8_t rtcStoresUtc;
  uint8_t rtcVariantHint;
  char timeZoneId[40];
};
RTC_DATA_ATTR ClockRetention clockState = {};

void drawDigit(GfxRenderer& gfx, int digit, int x, int y, int unit) {
  static constexpr uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  const uint8_t mask = digit < 0 ? 0x40 : masks[digit];
  const int width = 6 * unit;
  const int height = 10 * unit;
  const int gap = std::max(2, unit / 8);
  if (mask & 0x01) gfx.fillRect(x + unit, y, width - 2 * unit, unit - gap);
  if (mask & 0x02) gfx.fillRect(x + width - unit, y + unit, unit - gap, height / 2 - unit - gap);
  if (mask & 0x04) gfx.fillRect(x + width - unit, y + height / 2 + gap, unit - gap, height / 2 - unit - gap);
  if (mask & 0x08) gfx.fillRect(x + unit, y + height - unit, width - 2 * unit, unit - gap);
  if (mask & 0x10) gfx.fillRect(x, y + height / 2 + gap, unit - gap, height / 2 - unit - gap);
  if (mask & 0x20) gfx.fillRect(x, y + unit, unit - gap, height / 2 - unit - gap);
  if (mask & 0x40) gfx.fillRect(x + unit, y + height / 2 - unit / 2, width - 2 * unit, unit - gap);
}

void renderClockFrame(GfxRenderer& gfx, time_t now) {
  tm local = {};
  const bool valid = halClock.isSystemTimeValid() && localtime_r(&now, &local) != nullptr;
  const bool use12Hour = clockState.timeFormat == CrossPointSettings::TIME_12H;
  const unsigned hour = ClockFormat::displayHour(local.tm_hour, use12Hour);
  const int width = gfx.getScreenWidth();
  const int height = gfx.getScreenHeight();
  const int unit = std::max(4, std::min((width - 96) / 29, (height - 200) / 10));
  const int left = (width - 29 * unit) / 2;
  const int top = (height - 10 * unit) / 2;
  gfx.clearScreen();
  const int digits[] = {valid ? static_cast<int>(hour / 10) : -1, valid ? static_cast<int>(hour % 10) : -1,
                        valid ? local.tm_min / 10 : -1, valid ? local.tm_min % 10 : -1};
  const int positions[] = {0, 7, 16, 23};
  for (int i = 0; i < 4; ++i) {
    if (i == 0 && valid && use12Hour && hour < 10) continue;
    drawDigit(gfx, digits[i], left + positions[i] * unit, top, unit);
  }
  gfx.fillRect(left + 14 * unit, top + 3 * unit, unit, unit);
  gfx.fillRect(left + 14 * unit, top + 6 * unit, unit, unit);

  char date[32] = {};
  if (valid) strftime(date, sizeof(date), "%Y-%m-%d", &local);
  gfx.drawCenteredText(UI_12_FONT_ID, top - 48, valid ? date : tr(STR_CLOCK_SET_TIME));
  if (valid && use12Hour) {
    gfx.drawCenteredText(UI_12_FONT_ID, top + 10 * unit + 12, ClockFormat::period(local.tm_hour));
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
  timeval now = {};
  gettimeofday(&now, nullptr);
  const uint64_t waitUs = DeskClockTime::untilNextMinuteUs(now.tv_sec, now.tv_usec);

  display.deepSleep();                 // Turn off the e-paper power rails, retain the image.
  Board::deinitForSleep();             // Also leave backlight, GPS/LoRa and SD bus inactive.
  pinMode(BoardPins::PowerButton, INPUT_PULLUP);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const esp_err_t timerResult = esp_sleep_enable_timer_wakeup(waitUs);
  esp_err_t buttonResult = ESP_FAIL;
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
  do {
    gettimeofday(&before, nullptr);
    const time_t previousDisplayedMinute = static_cast<time_t>(clockState.displayedMinuteEpoch);
    drawClock(gfx, before.tv_sec, fullRefresh, previousDisplayedMinute);
    clockState.displayedMinuteEpoch = (before.tv_sec / 60) * 60;
    gettimeofday(&after, nullptr);
    // An unusually slow refresh can cross a minute boundary. Never sleep for
    // another minute showing the previous one.
  } while (before.tv_sec / 60 != after.tv_sec / 60);

  // If the button was pressed while painting, leave clock mode and let normal
  // startup handle that press, instead of sleeping through it.
  if (digitalRead(BoardPins::PowerButton) == LOW) {
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
  input.update();
  // Don't immediately wake from the press that entered clock mode.
  while (digitalRead(BoardPins::PowerButton) == LOW) {
    delay(10);
    input.update();
  }

  clockState = {};
  clockState.magic = kClockMagic;
  clockState.rtcReferenceEpoch = SETTINGS.rtcReferenceEpoch;
  clockState.timeFormat = SETTINGS.timeFormat;
  clockState.language = SETTINGS.language;
  clockState.flipUi = SETTINGS.flipUi;
  clockState.rtcStoresUtc = SETTINGS.rtcStoresUtc;
  clockState.rtcVariantHint = SETTINGS.rtcVariantHint;
  memcpy(clockState.timeZoneId, SETTINGS.timeZoneId, sizeof(clockState.timeZoneId));
  clockState.timeZoneId[sizeof(clockState.timeZoneId) - 1] = '\0';

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  Board::setBacklightLevel(0);
  halTiltSensor.deepSleep();
  paintAndSleep(gfx, false);
}

bool DeskClockSleep::resumeAfterTimerWake() {
  const bool timerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  if (!timerWake || clockState.magic != kClockMagic) {
    clockState.magic = 0;  // Power button, reset, panic or normal boot exits clock.
    return false;
  }

  // Fast resume: no SD mount, app discovery, background tasks, touch setup,
  // network setup or full firmware UI boot on each minute.
  Board::begin();
  halClock.begin();
  halClock.configure(clockState.timeZoneId, clockState.rtcStoresUtc != 0,
                     clockState.rtcVariantHint, clockState.rtcReferenceEpoch);
  if (!halClock.syncSystemTimeFromRtc() && !halClock.isSystemTimeValid()) {
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
