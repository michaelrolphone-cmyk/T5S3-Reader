#pragma once

#include <string>

#include "activities/Activity.h"

/**
 * Settings mode uses the native SD Firmware Update ELF after firmware-owned
 * .bin selection. Recovery mode intentionally remains fully firmware-native so
 * boot recovery never depends on an external app being present on the SD card.
 */
class SdFirmwareUpdateActivity : public Activity {
 public:
  enum class State { PICKING, VALIDATING, CONFIRMING, UPDATING, SUCCESS, FAILED };

  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool recoveryMode = false)
      : Activity("SdFirmwareUpdate", renderer, mappedInput), recoveryMode(recoveryMode) {}

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::UPDATING || state == State::VALIDATING; }
  bool skipLoopDelay() override { return state == State::UPDATING; }
  bool supportsGlobalMenu() const override { return false; }

 private:
  State state = State::PICKING;
  bool recoveryMode = false;
  bool launchFailed = false;
  std::string firmwarePath;
  size_t firmwareSize = 0;
  size_t writtenBytes = 0;
  unsigned int lastRenderedPercent = 101;
  std::string errorMessage;

  void launchPicker();
  void onPickerResult(const ActivityResult& result);
  bool validateFirmware();
  void promptConfirmation();
  void onConfirmationResult(const ActivityResult& result);
  void performUpdate();
};
