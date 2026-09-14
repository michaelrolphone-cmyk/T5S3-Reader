#pragma once

#include "activities/Activity.h"

class KOReaderAuthActivity final : public Activity {
 public:
  explicit KOReaderAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("KOReaderAuth", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return !launchAttempted || !launchFailed; }

 private:
  bool launchAttempted = false;
  bool launchFailed = false;
};
