#include "RequiredAppActivity.h"

#include <GfxRenderer.h>

#include <utility>

#include "../../MappedInputManager.h"
#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "../../native/NativeAppHost.h"

RequiredAppActivity::RequiredAppActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput,
    std::string artifactName, std::string appName)
    : Activity("RequiredApp", renderer, mappedInput),
      artifact(std::move(artifactName)),
      displayName(std::move(appName)) {}

void RequiredAppActivity::onEnter() {
  Activity::onEnter();
  if (displayName.empty()) displayName = artifact;
  status = "Install it now to continue this workflow.";
  requestUpdate(true);
}

void RequiredAppActivity::install() {
  if (installing) return;
  installing = true;
  status = "Connecting and installing...";
  requestUpdateAndWait();

  std::string catalogName;
  std::string failure;
  const bool installed = installRequiredNativeApp(
      artifact.c_str(), catalogName, failure);
  if (!catalogName.empty()) displayName = std::move(catalogName);
  installing = false;

  if (installed) {
    ActivityResult result;
    result.isCancelled = false;
    setResult(std::move(result));
    finish();
    return;
  }

  status = failure.empty() ? "Installation failed. Select Retry to try again."
                           : std::move(failure);
  requestUpdate();
}

void RequiredAppActivity::cancel() {
  if (installing) return;
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void RequiredAppActivity::loop() {
  if (installing) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    install();
  }
}

bool RequiredAppActivity::onTouchTap(int16_t x, int16_t y) {
  if (installing) return true;
  MappedInputManager::Button button = MappedInputManager::Button::Back;
  if (!resolveTouchButtonHint(x, y, button)) return false;
  if (button == MappedInputManager::Button::Back) {
    cancel();
    return true;
  }
  if (button == MappedInputManager::Button::Confirm) {
    install();
    return true;
  }
  return false;
}

void RequiredAppActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight},
                 "Required App");

  const std::string title = renderer.truncatedText(
      UI_12_FONT_ID, displayName.c_str(), width - 48, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, y, title.c_str(), true,
                            EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, y + 42,
                            "This workflow needs this app to continue.");

  const std::string safeStatus = renderer.truncatedText(
      SMALL_FONT_ID, status.c_str(), width - 48);
  renderer.drawCenteredText(SMALL_FONT_ID, y + 82, safeStatus.c_str());

  const auto labels = installing
      ? mappedInput.mapLabels("", "", "", "")
      : mappedInput.mapLabels("Back", status.rfind("Install", 0) == 0 ? "Install" : "Retry", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
}
