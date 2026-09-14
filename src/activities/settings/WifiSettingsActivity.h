#pragma once

#include "activities/Activity.h"

class WifiSettingsActivity final : public Activity {
 public:
  explicit WifiSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("WifiSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;

 private:
  bool launchAttempted = false;
  bool launchFailed = false;
};
