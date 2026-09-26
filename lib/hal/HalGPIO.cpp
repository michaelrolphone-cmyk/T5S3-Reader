#include <HalGPIO.h>

#include <Logging.h>
#include <esp_sleep.h>

// Global HalGPIO instance
HalGPIO gpio;

namespace {
constexpr uint16_t TOUCH_SWIPE_THRESHOLD = 25;
constexpr unsigned long TOUCH_HOME_BUTTON_DEBOUNCE_MS = 40;
constexpr UBaseType_t TOUCH_TAP_QUEUE_DEPTH = 16;
constexpr UBaseType_t TOUCH_SWIPE_QUEUE_DEPTH = 8;
constexpr UBaseType_t TOUCH_HOME_QUEUE_DEPTH = 16;
constexpr uint64_t POWER_WAKE_MASK = 1ULL << BoardPins::PowerButton;
constexpr uint64_t TOUCH_WAKE_MASK = 1ULL << BoardPins::TouchInterrupt;

uint8_t buttonBit(uint8_t button) { return static_cast<uint8_t>(1U << button); }

void rotatePhysicalTouchToLogical(uint16_t* x, uint16_t* y) {
#if defined(BOARD_LILYGO_EPD47_S3)
  const uint16_t physicalX = *x;
  const uint16_t physicalY = *y;
  *x = physicalY < BoardPins::LogicalWidth ? BoardPins::LogicalWidth - 1 - physicalY : 0;
  *y = physicalX < BoardPins::LogicalHeight ? physicalX : BoardPins::LogicalHeight - 1;
#else
  (void)x;
  (void)y;
#endif
}
}  // namespace

void HalGPIO::begin() {
  Board::begin();
  const bool touchReady = touch.begin();

  if (touchReady) {
    touchTapQueue = xQueueCreate(TOUCH_TAP_QUEUE_DEPTH, sizeof(TouchPoint));
    touchSwipeQueue = xQueueCreate(TOUCH_SWIPE_QUEUE_DEPTH, sizeof(TouchSwipeEvent));
    touchHomeQueue = xQueueCreate(TOUCH_HOME_QUEUE_DEPTH, sizeof(unsigned long));
    if (touchTapQueue && touchSwipeQueue && touchHomeQueue &&
        xTaskCreate(touchTaskTrampoline, "touch-input", 4096, this, 4,
                    &touchTaskHandle) == pdPASS) {
      pinMode(BoardPins::TouchInterrupt, INPUT_PULLUP);
      attachInterruptArg(BoardPins::TouchInterrupt, touchInterruptThunk, this, CHANGE);
      touchAsyncReady = true;
      // Drain any READY report that appeared between controller init and ISR setup.
      xTaskNotifyGive(touchTaskHandle);
    } else {
      LOG_ERR("HW", "Touch interrupt service failed to start");
    }
  }

  LOG_INF("HW", "Board init: id=%s pca9535=%d touch=%d async=%d usb=%d",
          Board::id(), Board::pca9535Present(), touchReady, touchAsyncReady,
          Board::isUsbConnected());

  lastUsbConnected = isUsbConnected();
  lastUsbPollTime = millis();
  update();
}

void IRAM_ATTR HalGPIO::touchInterruptThunk(void* context) {
  auto* self = static_cast<HalGPIO*>(context);
  if (!self || !self->touchTaskHandle) return;
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(self->touchTaskHandle, &higherPriorityTaskWoken);
  if (higherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

void HalGPIO::touchTaskTrampoline(void* context) {
  auto* self = static_cast<HalGPIO*>(context);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    self->serviceTouchController();
  }
}

void HalGPIO::serviceTouchController() {
  // A GT911 READY report stays pending until its status register is cleared.
  // Drain all reports currently available after one interrupt notification.
  for (unsigned i = 0; i < 16; ++i) {
    Board::TouchPoint point{};
    bool homeButtonPressed = false;
    bool contactActive = false;
    if (!touch.readEvent(&point, &homeButtonPressed, &contactActive)) break;
    processTouchEvent(point, homeButtonPressed, contactActive);
  }
}

void HalGPIO::processTouchEvent(const Board::TouchPoint& point,
                                bool touchHomeButtonPressed,
                                bool contactActive) {
  const unsigned long now = millis();

  if (touchHomeButtonPressed) {
    if (!touchHomeButtonHeld &&
        now - lastTouchHomeButtonEventTime >= TOUCH_HOME_BUTTON_DEBOUNCE_MS) {
      const unsigned long eventMs = now;
      (void)xQueueSend(touchHomeQueue, &eventMs, 0);
      lastTouchHomeButtonEventTime = now;
    }
    touchHomeButtonHeld = true;
  } else {
    touchHomeButtonHeld = false;
  }

  if (!contactActive) {
    TouchPoint tap{};
    TouchSwipeEvent swipe{};
    bool emitTap = false;
    bool emitSwipe = false;
    portENTER_CRITICAL(&touchStateMux);
    if (touchActive) {
      if (!touchMoved) {
        tap = {touchStartX, touchStartY};
        emitTap = true;
      } else {
        swipe = {{touchStartX, touchStartY}, currentTouchPoint};
        emitSwipe = true;
      }
      touchActive = false;
    }
    portEXIT_CRITICAL(&touchStateMux);
    if (emitTap) (void)xQueueSend(touchTapQueue, &tap, 0);
    if (emitSwipe) (void)xQueueSend(touchSwipeQueue, &swipe, 0);
    return;
  }

  uint16_t x = point.x;
  uint16_t y = point.y;
  rotatePhysicalTouchToLogical(&x, &y);

  portENTER_CRITICAL(&touchStateMux);
  if (!touchActive) {
    touchActive = true;
    touchStartX = x;
    touchStartY = y;
    currentTouchPoint = {x, y};
    touchStartTime = now;
    touchMoved = false;
  } else {
    currentTouchPoint = {x, y};
    const int dx = static_cast<int>(x) - static_cast<int>(touchStartX);
    const int dy = static_cast<int>(y) - static_cast<int>(touchStartY);
    if (abs(dx) >= TOUCH_SWIPE_THRESHOLD || abs(dy) >= TOUCH_SWIPE_THRESHOLD)
      touchMoved = true;
  }
  portEXIT_CRITICAL(&touchStateMux);
}

uint8_t HalGPIO::getState() {
  uint8_t state = 0;

  if (Board::readButton()) {
    state |= buttonBit(BTN_PCA);
  }
  if (digitalRead(BoardPins::PowerButton) == LOW) {
    state |= buttonBit(BTN_POWER);
  }

  return state;
}

void HalGPIO::update() {
  const unsigned long currentTime = millis();
  const uint8_t state = getState();

  pressedEvents = 0;
  releasedEvents = 0;

  if (state != lastState) {
    lastDebounceTime = currentTime;
    lastState = state;
  }

  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (state != currentState) {
      pressedEvents = state & ~currentState;
      releasedEvents = currentState & ~state;

      if (pressedEvents > 0 && currentState == 0) {
        buttonPressStart = currentTime;
      }
      if (releasedEvents > 0 && state == 0) {
        buttonPressFinish = currentTime;
      }

      currentState = state;
    }
  }

  usbStateChanged = false;
  if (static_cast<unsigned long>(currentTime - lastUsbPollTime) >= USB_STATE_POLL_MS) {
    lastUsbPollTime = currentTime;
    const bool connected = isUsbConnected();
    usbStateChanged = (connected != lastUsbConnected);
    lastUsbConnected = connected;
  }
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return currentState & buttonBit(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return pressedEvents & buttonBit(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return pressedEvents > 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return releasedEvents & buttonBit(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return releasedEvents > 0; }

bool HalGPIO::hadTouchActivity() const {
  bool active = false;
  portENTER_CRITICAL(&touchStateMux);
  active = touchActive;
  portEXIT_CRITICAL(&touchStateMux);
  return active ||
         (touchTapQueue && uxQueueMessagesWaiting(touchTapQueue)) ||
         (touchSwipeQueue && uxQueueMessagesWaiting(touchSwipeQueue)) ||
         (touchHomeQueue && uxQueueMessagesWaiting(touchHomeQueue));
}

bool HalGPIO::getTouchTap(TouchPoint& point) const {
  return touchTapQueue && xQueueReceive(touchTapQueue, &point, 0) == pdTRUE;
}

bool HalGPIO::getTouchHold(TouchPoint& point, unsigned long& heldMs) const {
  bool active = false;
  bool moved = false;
  unsigned long started = 0;
  portENTER_CRITICAL(&touchStateMux);
  active = touchActive;
  moved = touchMoved;
  point = currentTouchPoint;
  started = touchStartTime;
  portEXIT_CRITICAL(&touchStateMux);
  if (!active || moved) return false;
  heldMs = millis() - started;
  return true;
}

bool HalGPIO::getTouchSwipe(TouchPoint& start, TouchPoint& end) const {
  if (!touchSwipeQueue) return false;
  TouchSwipeEvent event{};
  if (xQueueReceive(touchSwipeQueue, &event, 0) != pdTRUE) return false;
  start = event.start;
  end = event.end;
  return true;
}

bool HalGPIO::takeTouchHomeButtonPress(unsigned long& eventMs) const {
  if (!touchHomeQueue) return false;
  return xQueueReceive(touchHomeQueue, &eventMs, 0) == pdTRUE;
}

bool HalGPIO::wasTouchHomeButtonPressed() const {
  unsigned long eventMs = 0;
  return takeTouchHomeButtonPress(eventMs);
}

unsigned long HalGPIO::getHeldTime() const {
  if (currentState > 0) {
    return millis() - buttonPressStart;
  }
  return buttonPressFinish - buttonPressStart;
}

void HalGPIO::startDeepSleep(bool wakeOnTouch) {
  while (isPressed(BTN_POWER)) {
    delay(50);
    update();
  }

  Board::deinitForSleep();
  pinMode(BoardPins::PowerButton, INPUT_PULLUP);
  pinMode(BoardPins::TouchInterrupt, INPUT_PULLUP);
  const bool enableTouchWake = wakeOnTouch && Board::capabilities().hasTouchWake;
  const uint64_t wakeMask = enableTouchWake ? (POWER_WAKE_MASK | TOUCH_WAKE_MASK) : POWER_WAKE_MASK;
  LOG_DBG("GPIO", "Entering deep sleep, wakeOnTouch=%d", wakeOnTouch ? 1 : 0);
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  esp_deep_sleep_start();
}

void HalGPIO::verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed) {
  if (shortPressAllowed) {
    return;
  }

  const uint16_t calibration = millis();
  const uint16_t calibratedDuration = calibration < requiredDurationMs ? requiredDurationMs - calibration : 1;

  const auto start = millis();
  update();
  while (!isPressed(BTN_POWER) && millis() - start < 1000) {
    delay(10);
    update();
  }

  if (isPressed(BTN_POWER)) {
    do {
      delay(10);
      update();
    } while (isPressed(BTN_POWER) && getHeldTime() < calibratedDuration);
    if (getHeldTime() < calibratedDuration) {
      startDeepSleep();
    }
  } else {
    startDeepSleep();
  }
}

bool HalGPIO::isUsbConnected() const { return Board::isUsbConnected(); }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();
  const bool usbConnected = isUsbConnected();

  if (wakeupCause == ESP_SLEEP_WAKEUP_EXT1) {
    const uint64_t wakeStatus = esp_sleep_get_ext1_wakeup_status();
    if ((wakeStatus & POWER_WAKE_MASK) != 0) {
      return WakeupReason::PowerButton;
    }
    if ((wakeStatus & TOUCH_WAKE_MASK) != 0) {
      return WakeupReason::Touch;
    }
  }

  if (wakeupCause == ESP_SLEEP_WAKEUP_GPIO) {
    if (digitalRead(BoardPins::PowerButton) == LOW) {
      return WakeupReason::PowerButton;
    }
    if (digitalRead(BoardPins::TouchInterrupt) == LOW) {
      return WakeupReason::Touch;
    }
    return WakeupReason::Other;
  }

  if (resetReason == ESP_RST_POWERON && !usbConnected) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
