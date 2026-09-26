#pragma once

#include <string>

#include "../Activity.h"

// Firmware-owned gate for a workflow step that requires an installable native
// application. The parent Activity stays on the stack. Confirm installs the
// exact requested artifact from the authoritative catalog; success returns to
// the parent so it can repeat the original workflow step immediately.
class RequiredAppActivity final : public Activity {
 public:
  RequiredAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                      std::string artifact, std::string displayName);

  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  std::string artifact;
  std::string displayName;
  std::string status;
  bool installing = false;

  void install();
  void cancel();
};
