#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Normal Settings mode delegates browsing to the installable File Browser app.
 * Recovery mode intentionally retains only a narrow firmware-owned .bin chooser
 * so boot recovery does not depend on any external app being installed.
 */
class SdFirmwareUpdateActivity : public Activity {
 public:
  enum class State { OPENING_BROWSER, PICKING, VALIDATING, CONFIRMING, UPDATING, SUCCESS, FAILED };

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
  State state = State::OPENING_BROWSER;
  bool recoveryMode = false;
  bool launchFailed = false;
  bool installRetry = false;
  bool browserPending = false;
  bool browserFinished = false;
  std::string browserPath;

  ButtonNavigator buttonNavigator;
  std::string pickerPath = "/";
  std::vector<std::string> pickerEntries;
  size_t pickerIndex = 0;

  std::string firmwarePath;
  size_t firmwareSize = 0;
  size_t writtenBytes = 0;
  unsigned int lastRenderedPercent = 101;
  std::string errorMessage;

  void launchFileBrowser();
  void runFileBrowser();
  void loadRecoveryEntries();
  void openRecoveryEntry();
  void goRecoveryUp();
  bool validateFirmware();
  void promptConfirmation();
  void onConfirmationResult(const ActivityResult& result);
  void performUpdate();
};
