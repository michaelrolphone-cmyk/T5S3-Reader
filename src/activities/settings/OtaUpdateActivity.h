#pragma once

#include "activities/Activity.h"

class OtaUpdateActivity : public Activity {
  enum State {
    WIFI_SELECTION,
    LAUNCH_PENDING,
    LAUNCH_FAILED,
  };

  State state = WIFI_SELECTION;
  bool launchAttempted = false;

  void onWifiSelectionComplete(bool success);

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == LAUNCH_PENDING; }
  bool skipLoopDelay() override { return true; }
};
