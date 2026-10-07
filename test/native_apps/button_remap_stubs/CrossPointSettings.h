#pragma once

#include <stdint.h>

// Only the settings surface used by the real NativeButtonRemapBridge.cpp.
// The harness supplies persistence and records every attempted write.
class CrossPointSettings {
 public:
  enum : uint8_t {
    FRONT_HW_BACK = 0,
    FRONT_HW_CONFIRM = 1,
    FRONT_HW_LEFT = 2,
    FRONT_HW_RIGHT = 3,
  };

  uint8_t frontButtonBack = FRONT_HW_BACK;
  uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  uint8_t frontButtonLeft = FRONT_HW_LEFT;
  uint8_t frontButtonRight = FRONT_HW_RIGHT;

  static CrossPointSettings& getInstance();
  bool saveToFile() const;
};

#define SETTINGS CrossPointSettings::getInstance()
