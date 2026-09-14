#pragma once

#include "activities/Activity.h"

class ButtonRemapActivity final : public Activity {
 public:
  explicit ButtonRemapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ButtonRemap", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  bool supportsTouchButtonHints() const override { return false; }
  void render(RenderLock&&) override;

 private:
  bool launchAttempted = false;
  bool launchFailed = false;
};
