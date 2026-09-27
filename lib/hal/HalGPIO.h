#pragma once

#include <Arduino.h>
#include <Board.h>
class HalGPIO {
 public:
  struct TouchPoint {
    uint16_t x = 0;
    uint16_t y = 0;
  };

 private:
  uint8_t currentState = 0;
  uint8_t lastState = 0;
  uint8_t pressedEvents = 0;
  uint8_t releasedEvents = 0;
  unsigned long lastDebounceTime = 0;
  unsigned long buttonPressStart = 0;
  unsigned long buttonPressFinish = 0;

  bool lastUsbConnected = false;
  bool usbStateChanged = false;
  unsigned long lastUsbPollTime = 0;

  uint8_t getState();
 public:
  enum class DeviceType : uint8_t { T5S3Pro, LilyGoEPD47 };

  HalGPIO() = default;

  inline bool deviceIsX3() const { return false; }
  inline bool deviceIsX4() const { return false; }
  inline bool deviceIsT5S3() const { return Device == DeviceType::T5S3Pro; }
  inline bool deviceIsEpd47() const { return Device == DeviceType::LilyGoEPD47; }
  inline const char* getDeviceName() const { return Board::displayName(); }

  void begin();
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  void startDeepSleep(bool wakeOnTouch = true);
  void verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);

  bool isUsbConnected() const;
  bool wasUsbStateChanged() const;

  enum class WakeupReason { PowerButton, Touch, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
  static constexpr uint8_t BTN_PCA = 7;

 private:
#if defined(BOARD_LILYGO_EPD47_S3)
  static constexpr DeviceType Device = DeviceType::LilyGoEPD47;
#else
  static constexpr DeviceType Device = DeviceType::T5S3Pro;
#endif
  static constexpr unsigned long DEBOUNCE_DELAY = 5;
  static constexpr unsigned long USB_STATE_POLL_MS = 250;
};

extern HalGPIO gpio;
