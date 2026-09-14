#pragma once

#include "activities/Activity.h"

class FontDownloadActivity : public Activity {
 public:
  explicit FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FontDownload", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;

 private:
  bool launchAttempted = false;
  bool launchFailed = false;
};
