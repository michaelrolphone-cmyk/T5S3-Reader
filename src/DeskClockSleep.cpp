#include "DeskClockSleep.h"

#include <Board.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <sys/time.h>

#include <algorithm>
#include <ctime>

#include "activities/RenderLock.h"
#include "fontIds.h"
#include "util/DeskClockTime.h"

namespace {
void drawDigit(GfxRenderer& renderer, int digit, int x, int y, int unit) {
  // a,b,c,d,e,f,g: top, upper right, lower right, bottom, lower left,
  // upper left, middle. A negative digit draws a dash for unset time.
  static constexpr uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  const uint8_t mask = digit < 0 ? 0x40 : masks[digit];
  const int width = 6 * unit;
  const int height = 10 * unit;
  const int gap = std::max(2, unit / 8);
  if (mask & 0x01) renderer.fillRect(x + unit, y, width - 2 * unit, unit - gap);
  if (mask & 0x02) renderer.fillRect(x + width - unit, y + unit, unit - gap, height / 2 - unit - gap);
  if (mask & 0x04) renderer.fillRect(x + width - unit, y + height / 2 + gap, unit - gap, height / 2 - unit - gap);
  if (mask & 0x08) renderer.fillRect(x + unit, y + height - unit, width - 2 * unit, unit - gap);
  if (mask & 0x10) renderer.fillRect(x, y + height / 2 + gap, unit - gap, height / 2 - unit - gap);
  if (mask & 0x20) renderer.fillRect(x, y + unit, unit - gap, height / 2 - unit - gap);
  if (mask & 0x40) renderer.fillRect(x + unit, y + height / 2 - unit / 2, width - 2 * unit, unit - gap);
}

void drawClock(GfxRenderer& renderer, const time_t now, bool touchWake, bool first) {
  tm local = {};
  const bool valid = halClock.isSystemTimeValid() && localtime_r(&now, &local) != nullptr;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int unit = std::max(4, std::min((width - 96) / 29, (height - 160) / 10));
  const int left = (width - 29 * unit) / 2;
  const int top = (height - 10 * unit) / 2;
  renderer.clearScreen();
  const int digits[] = {valid ? local.tm_hour / 10 : -1, valid ? local.tm_hour % 10 : -1,
                        valid ? local.tm_min / 10 : -1, valid ? local.tm_min % 10 : -1};
  const int positions[] = {0, 7, 16, 23};
  for (int i = 0; i < 4; ++i) drawDigit(renderer, digits[i], left + positions[i] * unit, top, unit);
  renderer.fillRect(left + 14 * unit, top + 3 * unit, unit, unit);
  renderer.fillRect(left + 14 * unit, top + 6 * unit, unit, unit);

  char date[32] = {};
  if (valid) strftime(date, sizeof(date), "%Y-%m-%d", &local);
  renderer.drawCenteredText(UI_12_FONT_ID, top - 48, valid ? date : tr(STR_CLOCK_SET_TIME));
  renderer.drawCenteredText(SMALL_FONT_ID, height - 44,
                           touchWake ? tr(STR_CLOCK_WAKE) : tr(STR_CLOCK_WAKE_BUTTON));
  display.setIdlePowerSaving(false);
  renderer.displayBuffer(first ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH);
  display.setIdlePowerSaving(true);
}
}  // namespace

void DeskClockSleep::run(GfxRenderer& renderer, HalGPIO& input) {
  // This is a dedicated sleep loop, not an activity's loop()/render() callback.
  // The render task must not modify the framebuffer or hold a peripheral active
  // while the main task puts both CPUs into light sleep.
  RenderLock lock;
  const auto previousOrientation = renderer.getOrientation();
  const auto previousMode = renderer.getRenderMode();
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  renderer.setRenderMode(GfxRenderer::BW);
  WiFi.mode(WIFI_OFF);

  const gpio_num_t powerPin = static_cast<gpio_num_t>(BoardPins::PowerButton);
  const gpio_num_t touchPin = static_cast<gpio_num_t>(BoardPins::TouchInterrupt);
  const bool touchWake = Board::capabilities().hasTouchWake && input.isTouchAvailable();
  // Drain the entry press and any pending touch report before arming level wake.
  input.update();
  while (digitalRead(BoardPins::PowerButton) == LOW) {
    delay(10);
    input.update();
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  bool wakeReady = gpio_wakeup_enable(powerPin, GPIO_INTR_LOW_LEVEL) == ESP_OK;
  if (touchWake) wakeReady = (gpio_wakeup_enable(touchPin, GPIO_INTR_LOW_LEVEL) == ESP_OK) && wakeReady;
  wakeReady = (esp_sleep_enable_gpio_wakeup() == ESP_OK) && wakeReady;

  int64_t displayedMinute = -1;
  bool first = true;
  while (wakeReady) {
    timeval now = {};
    gettimeofday(&now, nullptr);
    const int64_t minute = static_cast<int64_t>(now.tv_sec) / 60;
    if (first || minute != displayedMinute) {
      drawClock(renderer, now.tv_sec, touchWake, first);
      displayedMinute = minute;
      first = false;
    }

    // A touch may arrive during the panel refresh, before light sleep is armed.
    input.update();
    if (digitalRead(BoardPins::PowerButton) == LOW || input.hadTouchActivity() || input.wasTouchHomeButtonPressed()) break;

    gettimeofday(&now, nullptr);
    // If a slow display operation crossed a minute, redraw immediately instead
    // of sleeping through that minute with the old value on the screen.
    if (static_cast<int64_t>(now.tv_sec) / 60 != displayedMinute) continue;
    const uint64_t waitUs = DeskClockTime::untilNextMinuteUs(now.tv_sec, now.tv_usec);
    if (esp_sleep_enable_timer_wakeup(waitUs) != ESP_OK) {
      LOG_ERR("CLOCK", "Could not arm minute timer");
      break;
    }
    esp_task_wdt_reset();
    const esp_err_t result = esp_light_sleep_start();
    esp_task_wdt_reset();
    if (result != ESP_OK) {
      LOG_ERR("CLOCK", "Light sleep failed: %s", esp_err_to_name(result));
      break;
    }
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) break;
  }
  if (!wakeReady) LOG_ERR("CLOCK", "Could not configure user wake pins");
  gpio_wakeup_disable(powerPin);
  if (touchWake) gpio_wakeup_disable(touchPin);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  display.setIdlePowerSaving(false);
  renderer.setOrientation(previousOrientation);
  renderer.setRenderMode(previousMode);

  // Consume the wake gesture, including the touch release grace interval, so
  // it cannot activate a control on the restored screen.
  unsigned long quietSince = millis();
  do {
    input.update();
    if (input.isPressed(HalGPIO::BTN_POWER) || input.hadTouchActivity() || input.wasTouchHomeButtonPressed()) {
      quietSince = millis();
    }
    delay(10);
  } while (millis() - quietSince < 350);
}
