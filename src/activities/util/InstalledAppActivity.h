#pragma once

#include <string>

#include "../Activity.h"

// Firmware-owned lifecycle wrapper for transitioning from a built-in screen to
// an installable app. It contains no app-specific UI or behavior: it resolves
// the verified artifact, offers inline installation when missing, runs the ELF,
// and returns Home if the app exits without requesting another navigation.
class InstalledAppActivity final : public Activity {
 public:
  InstalledAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                       std::string artifact, std::string displayName);

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;

 private:
  std::string artifact;
  std::string displayName;
  bool launchAttempted = false;
  bool launchFailed = false;
  bool appReturned = false;

  void launch();
};
