#include "InstalledAppActivity.h"

#include <GfxRenderer.h>

#include <utility>

#include "../../MappedInputManager.h"
#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "../../native/InstalledAppPath.h"
#include "../../native/NativeAppHost.h"
#include "RequiredAppActivity.h"

InstalledAppActivity::InstalledAppActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput,
    std::string artifactName, std::string appName)
    : Activity("InstalledApp", renderer, mappedInput),
      artifact(std::move(artifactName)),
      displayName(std::move(appName)) {}

void InstalledAppActivity::onEnter() {
  Activity::onEnter();
  launchAttempted = false;
  launchFailed = false;
  appReturned = false;
  requestUpdate();
}

void InstalledAppActivity::launch() {
  std::string path;
  if (!resolveInstalledAppPath(artifact.c_str(), path)) {
    startActivityForResult(
        std::make_unique<RequiredAppActivity>(
            renderer, mappedInput, artifact, displayName),
        [this](const ActivityResult& result) {
          if (result.isCancelled) {
            launchFailed = true;
          } else {
            launchAttempted = false;
            launchFailed = false;
          }
          requestUpdate();
        });
    return;
  }

  const esp_err_t result = runNativeApp(path.c_str(), renderer, mappedInput);
  if (result != ESP_OK) {
    launchFailed = true;
    requestUpdate();
    return;
  }

  // The app may have queued firmware navigation while it was running. Leave
  // this wrapper alive for the rest of the current ActivityManager iteration
  // so that queued transition wins. If no transition was queued, the next loop
  // finishes this top-level wrapper and ActivityManager returns Home.
  appReturned = true;
}

void InstalledAppActivity::loop() {
  if (appReturned) {
    finish();
    return;
  }
  if (!launchAttempted) {
    launchAttempted = true;
    launch();
    return;
  }
  if (!launchFailed) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

bool InstalledAppActivity::onTouchTap(int16_t x, int16_t y) {
  if (!launchFailed) return true;
  MappedInputManager::Button button = MappedInputManager::Button::Back;
  if (!resolveTouchButtonHint(x, y, button)) return false;
  if (button == MappedInputManager::Button::Back ||
      button == MappedInputManager::Button::Confirm) {
    finish();
  }
  return true;
}

void InstalledAppActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight},
                 displayName.c_str());

  const int y = height / 2;
  if (launchFailed) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Application could not be launched");
    const auto labels = mappedInput.mapLabels("Back", "OK", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, y, ("Opening " + displayName + "...").c_str());
  }
  renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
}
